/* Produced by texiweb from libavl.w. */

/* libavl - library for manipulation of binary trees.
   Copyright (C) 1998-2002, 2004 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
   See the GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.

   The author may be contacted at <blp@gnu.org> on the Internet, or
   write to Ben Pfaff, Stanford University, Computer Science Dept., 353
   Serra Mall, Stanford CA 94305, USA.
*/

#ifndef BST_H
#define BST_H 1

#include <stddef.h>

/* Function types. */
typedef int bst_comparison_func (const void *bst_a, const void *bst_b,
                                 void *bst_param);
typedef void bst_item_func (void *bst_item, void *bst_param);
typedef void *bst_copy_func (void *bst_item, void *bst_param);

#ifndef LIBAVL_ALLOCATOR
#define LIBAVL_ALLOCATOR
/* Memory allocator. */
struct libavl_allocator
  {
    void *(*libavl_malloc) (struct libavl_allocator *, size_t libavl_size);
    void (*libavl_free) (struct libavl_allocator *, void *libavl_block);
  };
#endif

/* Default memory allocator. */
extern struct libavl_allocator bst_allocator_default;
void *bst_malloc (struct libavl_allocator *, size_t);
void bst_free (struct libavl_allocator *, void *);

/* Maximum BST height. */
#ifndef BST_MAX_HEIGHT
#define BST_MAX_HEIGHT 32
#endif

/* Tree data structure. */
struct bst_table
  {
    struct bst_node *bst_root;          /* Tree's root. */
    bst_comparison_func *bst_compare;   /* Comparison function. */
    void *bst_param;                    /* Extra argument to |bst_compare|. */
    struct libavl_allocator *bst_alloc; /* Memory allocator. */
    size_t bst_count;                   /* Number of items in tree. */
    unsigned long bst_generation;       /* Generation number. */
  };

/* A binary search tree node. */
struct bst_node
  {
    struct bst_node *bst_link[2];   /* Subtrees. */
    void *bst_data;                 /* Pointer to data. */
  };

/* BST traverser structure. */
struct bst_traverser
  {
    struct bst_table *bst_table;        /* Tree being traversed. */
    struct bst_node *bst_node;          /* Current node in tree. */
    struct bst_node *bst_stack[BST_MAX_HEIGHT];
                                        /* All the nodes above |bst_node|. */
    size_t bst_height;                  /* Number of nodes in |bst_parent|. */
    unsigned long bst_generation;       /* Generation number. */
  };

/* Table functions. */
struct bst_table *bst_create (bst_comparison_func *, void *,
                              struct libavl_allocator *);
struct bst_table *bst_copy (const struct bst_table *, bst_copy_func *,
                            bst_item_func *, struct libavl_allocator *);
void bst_destroy (struct bst_table *, bst_item_func *);
void **bst_probe (struct bst_table *, void *);
void *bst_insert (struct bst_table *, void *);
void *bst_replace (struct bst_table *, void *);
void *bst_delete (struct bst_table *, const void *);
void *bst_find (const struct bst_table *, const void *);
void bst_assert_insert (struct bst_table *, void *);
void *bst_assert_delete (struct bst_table *, void *);

#define bst_count(table) ((size_t) (table)->bst_count)

/* Table traverser functions. */
void bst_t_init (struct bst_traverser *, struct bst_table *);
void *bst_t_first (struct bst_traverser *, struct bst_table *);
void *bst_t_last (struct bst_traverser *, struct bst_table *);
void *bst_t_find (struct bst_traverser *, struct bst_table *, void *);
void *bst_t_insert (struct bst_traverser *, struct bst_table *, void *);
void *bst_t_copy (struct bst_traverser *, const struct bst_traverser *);
void *bst_t_next (struct bst_traverser *);
void *bst_t_prev (struct bst_traverser *);
void *bst_t_cur (struct bst_traverser *);
void *bst_t_replace (struct bst_traverser *, void *);

/* Special BST functions. */
void bst_balance (struct bst_table *tree);

#endif /* bst.h */

/* Table assertion functions. */
#ifndef NDEBUG
#undef bst_assert_insert
#undef bst_assert_delete
#else
#define bst_assert_insert(table, item) bst_insert (table, item)
#define bst_assert_delete(table, item) bst_delete (table, item)
#endif
