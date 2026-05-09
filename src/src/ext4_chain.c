#include "ext4_chain.h"

#include "ext4_dir.h"
#include "ext4_io.h"
#include "ext4_path.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *block_ref_kind_name(enum ext4_block_ref_kind kind)
{
    switch (kind) {
    case EXT4_BLOCK_REF_DIRECT:
        return "direct";
    case EXT4_BLOCK_REF_SINGLE_INDIRECT:
        return "single-indirect";
    case EXT4_BLOCK_REF_SINGLE_INDIRECT_TABLE:
        return "single-indirect-table";
    default:
        return "unknown";
    }
}

static int add_ref(struct ext4_inode_chain *chain, uint64_t logical_index,
                   uint64_t block_index, enum ext4_block_ref_kind kind)
{
    struct ext4_block_ref *resized;
    size_t new_capacity;

    if (chain->ref_count == chain->ref_capacity) {
        new_capacity = (chain->ref_capacity == 0U) ? 16U : chain->ref_capacity * 2U;
        resized = realloc(chain->refs, new_capacity * sizeof(*chain->refs));
        if (resized == NULL) {
            fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
            return 1;
        }
        chain->refs = resized;
        chain->ref_capacity = new_capacity;
    }

    chain->refs[chain->ref_count].logical_index = logical_index;
    chain->refs[chain->ref_count].block_index = block_index;
    chain->refs[chain->ref_count].kind = kind;
    ++chain->ref_count;
    return 0;
}

static int collect_ref_callback(uint32_t inode_number, uint64_t logical_index,
                                uint64_t block_index,
                                enum ext4_block_ref_kind kind, void *user)
{
    struct ext4_inode_chain *chain = user;

    (void)inode_number;
    return add_ref(chain, logical_index, block_index, kind);
}

int ext4_build_chain_for_inode(const struct ext4_fs *fs, uint32_t inode_number,
                               struct ext4_inode_chain *chain)
{
    struct ext4_inode inode;

    memset(chain, 0, sizeof(*chain));
    if (ext4_read_inode(fs, inode_number, &inode) != 0) {
        return 1;
    }

    chain->inode_number = inode_number;
    chain->type = inode.type;

    return ext4_traverse_inode_blocks(fs, &inode, collect_ref_callback, chain,
                                      &chain->corruption_count);
}

void ext4_inode_chain_free(struct ext4_inode_chain *chain)
{
    if (chain == NULL) {
        return;
    }

    free(chain->refs);
    chain->refs = NULL;
    chain->ref_count = 0U;
    chain->ref_capacity = 0U;
}

static int add_chain(struct ext4_chain_analysis *analysis,
                     const struct ext4_inode_chain *chain)
{
    struct ext4_inode_chain *resized;
    size_t new_capacity;

    if (analysis->chain_count == analysis->chain_capacity) {
        new_capacity = (analysis->chain_capacity == 0U) ? 16U :
                       analysis->chain_capacity * 2U;
        resized = realloc(analysis->chains, new_capacity * sizeof(*analysis->chains));
        if (resized == NULL) {
            fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
            return 1;
        }
        analysis->chains = resized;
        analysis->chain_capacity = new_capacity;
    }

    analysis->chains[analysis->chain_count] = *chain;
    ++analysis->chain_count;
    return 0;
}

static struct ext4_block_owner *find_owner(struct ext4_chain_analysis *analysis,
                                           uint64_t block_index)
{
    size_t i;

    for (i = 0U; i < analysis->owner_count; ++i) {
        if (analysis->owners[i].block_index == block_index) {
            return &analysis->owners[i];
        }
    }

    return NULL;
}

static int owner_has_inode(const struct ext4_block_owner *owner,
                           uint32_t inode_number)
{
    size_t i;

    for (i = 0U; i < owner->owner_count; ++i) {
        if (owner->inode_numbers[i] == inode_number) {
            return 1;
        }
    }

    return 0;
}

static int add_owner_inode(struct ext4_block_owner *owner, uint32_t inode_number)
{
    uint32_t *resized;
    size_t new_capacity;

    if (owner_has_inode(owner, inode_number)) {
        return 0;
    }

    if (owner->owner_count == owner->owner_capacity) {
        new_capacity = (owner->owner_capacity == 0U) ? 4U :
                       owner->owner_capacity * 2U;
        resized = realloc(owner->inode_numbers,
                          new_capacity * sizeof(*owner->inode_numbers));
        if (resized == NULL) {
            fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
            return 1;
        }
        owner->inode_numbers = resized;
        owner->owner_capacity = new_capacity;
    }

    owner->inode_numbers[owner->owner_count] = inode_number;
    ++owner->owner_count;
    return 0;
}

static int add_block_owner(struct ext4_chain_analysis *analysis,
                           uint64_t block_index, uint32_t inode_number)
{
    struct ext4_block_owner *owner = find_owner(analysis, block_index);
    struct ext4_block_owner *resized;
    size_t new_capacity;

    if (owner == NULL) {
        if (analysis->owner_count == analysis->owner_capacity) {
            new_capacity = (analysis->owner_capacity == 0U) ? 32U :
                           analysis->owner_capacity * 2U;
            resized = realloc(analysis->owners,
                              new_capacity * sizeof(*analysis->owners));
            if (resized == NULL) {
                fprintf(stderr, "malloc: %s\n", strerror(ENOMEM));
                return 1;
            }
            analysis->owners = resized;
            analysis->owner_capacity = new_capacity;
        }

        owner = &analysis->owners[analysis->owner_count];
        owner->block_index = block_index;
        owner->inode_numbers = NULL;
        owner->owner_count = 0U;
        owner->owner_capacity = 0U;
        ++analysis->owner_count;
    }

    return add_owner_inode(owner, inode_number);
}

static int index_chain_owners(struct ext4_chain_analysis *analysis,
                              const struct ext4_inode_chain *chain)
{
    size_t i;

    for (i = 0U; i < chain->ref_count; ++i) {
        if (add_block_owner(analysis, chain->refs[i].block_index,
                            chain->inode_number) != 0) {
            return 1;
        }
    }

    return 0;
}

static int inode_appears_allocated(const struct ext4_fs *fs,
                                   uint32_t inode_number, int *allocated)
{
    unsigned char header[2];
    uint64_t offset;

    *allocated = 0;
    if (ext4_inode_offset(fs, inode_number, &offset) != 0) {
        return 1;
    }

    if (ext4_io_read_exact_at(fs->fd, offset, header, sizeof(header),
                              "read ext4 inode mode") != 0) {
        return 1;
    }

    *allocated = ext4_read_le16(header, 0U) != 0U;
    return 0;
}

int ext4_build_chain_analysis(const struct ext4_fs *fs,
                              struct ext4_chain_analysis *analysis)
{
    uint32_t inode_number;
    int status = 0;

    memset(analysis, 0, sizeof(*analysis));
    for (inode_number = 1U; inode_number <= fs->superblock.s_inodes_count;
         ++inode_number) {
        struct ext4_inode inode;
        struct ext4_inode_chain chain;
        int allocated = 0;

        if (inode_appears_allocated(fs, inode_number, &allocated) != 0) {
            analysis->corruption_count++;
            continue;
        }
        if (!allocated) {
            continue;
        }
        if (ext4_read_inode(fs, inode_number, &inode) != 0) {
            analysis->corruption_count++;
            continue;
        }

        if (inode.type != EXT4_INODE_TYPE_REGULAR &&
            inode.type != EXT4_INODE_TYPE_DIRECTORY) {
            continue;
        }

        if (ext4_build_chain_for_inode(fs, inode_number, &chain) != 0) {
            analysis->corruption_count++;
            continue;
        }

        analysis->corruption_count += chain.corruption_count;
        if (index_chain_owners(analysis, &chain) != 0 ||
            add_chain(analysis, &chain) != 0) {
            ext4_inode_chain_free(&chain);
            status = 1;
            break;
        }
    }

    return status;
}

void ext4_chain_analysis_free(struct ext4_chain_analysis *analysis)
{
    size_t i;

    if (analysis == NULL) {
        return;
    }

    for (i = 0U; i < analysis->chain_count; ++i) {
        ext4_inode_chain_free(&analysis->chains[i]);
    }
    for (i = 0U; i < analysis->owner_count; ++i) {
        free(analysis->owners[i].inode_numbers);
    }

    free(analysis->chains);
    free(analysis->owners);
    memset(analysis, 0, sizeof(*analysis));
}

int ext4_print_chain_for_path(const struct ext4_fs *fs, const char *path)
{
    uint32_t inode_number;
    struct ext4_inode_chain chain;
    size_t i;
    int status;

    if (ext4_resolve_path(fs, EXT4_ROOT_INO, path, &inode_number) != 0) {
        return 1;
    }

    status = ext4_build_chain_for_inode(fs, inode_number, &chain);
    if (status != 0) {
        return 1;
    }

    printf("inode %" PRIu32 " (%s) block chain for %s\n", chain.inode_number,
           ext4_inode_type_name(chain.type), path);
    for (i = 0U; i < chain.ref_count; ++i) {
        printf("  logical=%" PRIu64 " block=%" PRIu64 " kind=%s\n",
               chain.refs[i].logical_index, chain.refs[i].block_index,
               block_ref_kind_name(chain.refs[i].kind));
    }
    if (chain.corruption_count > 0) {
        printf("  corruptions: %d\n", chain.corruption_count);
    }

    ext4_inode_chain_free(&chain);
    return status;
}

int ext4_print_shared_blocks(const struct ext4_fs *fs)
{
    struct ext4_chain_analysis analysis;
    size_t i;
    int found = 0;
    int status;

    status = ext4_build_chain_analysis(fs, &analysis);
    for (i = 0U; i < analysis.owner_count; ++i) {
        size_t j;

        if (analysis.owners[i].owner_count < 2U) {
            continue;
        }

        found = 1;
        printf("shared block %" PRIu64 " owners:", analysis.owners[i].block_index);
        for (j = 0U; j < analysis.owners[i].owner_count; ++j) {
            printf(" %" PRIu32, analysis.owners[i].inode_numbers[j]);
        }
        printf("\n");
    }

    if (!found) {
        printf("no shared blocks detected\n");
    }

    ext4_chain_analysis_free(&analysis);
    return status;
}

int ext4_print_corruption_scan(const struct ext4_fs *fs)
{
    struct ext4_chain_analysis analysis;
    size_t i;
    int status;

    status = ext4_build_chain_analysis(fs, &analysis);
    printf("chain scan summary\n");
    printf("  inodes analyzed: %zu\n", analysis.chain_count);
    printf("  blocks indexed: %zu\n", analysis.owner_count);
    printf("  corruptions: %d\n", analysis.corruption_count);

    for (i = 0U; i < analysis.owner_count; ++i) {
        if (analysis.owners[i].owner_count > 1U) {
            printf("  recovery plan: duplicate shared block %" PRIu64
                   " for one of %zu owners\n",
                   analysis.owners[i].block_index,
                   analysis.owners[i].owner_count);
        }
    }

    for (i = 0U; i < analysis.chain_count; ++i) {
        if (analysis.chains[i].ref_count == 0U) {
            printf("  recovery plan: inode %" PRIu32
                   " has no data blocks; consider orphan cleanup if unlinked\n",
                   analysis.chains[i].inode_number);
        }
        if (analysis.chains[i].corruption_count > 0) {
            printf("  recovery plan: inspect inode %" PRIu32
                   " before copying or deleting blocks\n",
                   analysis.chains[i].inode_number);
        }
    }

    ext4_chain_analysis_free(&analysis);
    return status;
}
