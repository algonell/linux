// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (c) 2016-2021 Christoph Hellwig.
 */
#include <linux/fs.h>
#include <linux/iomap.h>
#include "trace.h"

static inline void iomap_iter_reset_iomap(struct iomap_iter *iter)
{
	iter->processed = 0;
	memset(&iter->iomap, 0, sizeof(iter->iomap));
	memset(&iter->srcmap, 0, sizeof(iter->srcmap));
}

/*
 * Advance the current iterator position and output the length remaining for the
 * current mapping.
 */
int iomap_iter_advance(struct iomap_iter *iter, u64 *count)
{
	if (WARN_ON_ONCE(*count > iomap_length(iter)))
		return -EIO;
	iter->pos += *count;
	iter->len -= *count;
	*count = iomap_length(iter);
	return 0;
}

static inline void iomap_iter_done(struct iomap_iter *iter)
{
	WARN_ON_ONCE(iter->iomap.offset > iter->pos);
	WARN_ON_ONCE(iter->iomap.length == 0);
	WARN_ON_ONCE(iter->iomap.offset + iter->iomap.length <= iter->pos);
	WARN_ON_ONCE(iter->iomap.flags & IOMAP_F_STALE);

	iter->iter_start_pos = iter->pos;

	trace_iomap_iter_dstmap(iter->inode, &iter->iomap);
	if (iter->srcmap.type != IOMAP_HOLE)
		trace_iomap_iter_srcmap(iter->inode, &iter->srcmap);
}

/**
 * iomap_iter - iterate over a ranges in a file
 * @iter: iteration structue
 * @ops: iomap ops provided by the file system
 *
 * Iterate over filesystem-provided space mappings for the provided file range.
 *
 * This function handles cleanup of resources acquired for iteration when the
 * filesystem indicates there are no more space mappings, which means that this
 * function must be called in a loop that continues as long it returns a
 * positive value.  If 0 or a negative value is returned, the caller must not
 * return to the loop body.  Within a loop body, there are two ways to break out
 * of the loop body:  leave @iter.processed unchanged, or set it to a negative
 * errno.
 */
int iomap_iter(struct iomap_iter *iter, const struct iomap_ops *ops)
{
	bool stale = iter->iomap.flags & IOMAP_F_STALE;
	ssize_t advanced = iter->processed > 0 ? iter->processed : 0;
	u64 olen = iter->len;
	s64 processed;
	int ret;

	trace_iomap_iter(iter, ops, _RET_IP_);

	if (!iter->iomap.length)
		goto begin;

	/*
	 * If iter.processed is zero, the op may still have advanced the iter
	 * itself. Calculate the advanced and original length bytes based on how
	 * far pos has advanced for ->iomap_end().
	 */
	if (!advanced) {
		advanced = iter->pos - iter->iter_start_pos;
		olen += advanced;
	}

	if (ops->iomap_end) {
		ret = ops->iomap_end(iter->inode, iter->iter_start_pos,
				iomap_length_trim(iter, iter->iter_start_pos,
						  olen),
				advanced, iter->flags, &iter->iomap);
		if (ret < 0 && !advanced)
			return ret;
	}

	processed = iter->processed;
	if (processed < 0) {
		iomap_iter_reset_iomap(iter);
		return processed;
	}

	/*
	 * Advance the iter and clear state from the previous iteration. This
	 * passes iter->processed because that reflects the bytes processed but
	 * not yet advanced by the iter handler.
	 *
	 * Use iter->len to determine whether to continue onto the next mapping.
	 * Explicitly terminate in the case where the current iter has not
	 * advanced at all (i.e. no work was done for some reason) unless the
	 * mapping has been marked stale and needs to be reprocessed.
	 */
	ret = iomap_iter_advance(iter, &processed);
	if (!ret && iter->len > 0)
		ret = 1;
	if (ret > 0 && !advanced && !stale)
		ret = 0;
	iomap_iter_reset_iomap(iter);
	if (ret <= 0)
		return ret;

begin:
	ret = ops->iomap_begin(iter->inode, iter->pos, iter->len, iter->flags,
			       &iter->iomap, &iter->srcmap);
	if (ret < 0)
		return ret;
	iomap_iter_done(iter);
	return 1;
}
