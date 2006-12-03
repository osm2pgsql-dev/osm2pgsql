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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bst.h"

/* Creates and returns a new table
   with comparison function |compare| using parameter |param|
   and memory allocator |allocator|.
   Returns |NULL| if memory allocation failed. */
struct bst_table *
bst_create (bst_comparison_func *compare, void *param,
            struct libavl_allocator *allocator)
{
  struct bst_table *tree;

  assert (compare != NULL);

  if (allocator == NULL)
    allocator = &bst_allocator_default;

  tree = allocator->libavl_malloc (allocator, sizeof *tree);
  if (tree == NULL)
    return NULL;

  tree->bst_root = NULL;
  tree->bst_compare = compare;
  tree->bst_param = param;
  tree->bst_alloc = allocator;
  tree->bst_count = 0;
  tree->bst_generation = 0;

  return tree;
}

/* Search |tree| for an item matching |item|, and return it if found.
   Otherwise return |NULL|. */
void *
bst_find (const struct bst_table *tree, const void *item)
{
  const struct bst_node *p;

  assert (tree != NULL && item != NULL);
  for (p = tree->bst_root; p != NULL; )
    {
      int cmp = tree->bst_compare (item, p->bst_data, tree->bst_param);

      if (cmp < 0)
        p = p->bst_link[0];
      else if (cmp > 0)
        p = p->bst_link[1];
      else /* |cmp == 0| */
        return p->bst_data;
    }

  return NULL;
}

/* Inserts |item| into |tree| and returns a pointer to |item|'s address.
   If a duplicate item is found in the tree,
   returns a pointer to the duplicate without inserting |item|.
   Returns |NULL| in case of memory allocation failure. */
void **
bst_probe (struct bst_table *tree, void *item)
{
  struct bst_node *p, *q; /* Current node in search and its parent. */
  int dir;                /* Side of |q| on which |p| is located. */
  struct bst_node *n;     /* Newly inserted node. */

  assert (tree != NULL && item != NULL);

  for (q = NULL, p = tree->bst_root; p != NULL; q = p, p = p->bst_link[dir])
    {
      int cmp = tree->bst_compare (item, p->bst_data, tree->bst_param);
      if (cmp == 0)
        return &p->bst_data;
      dir = cmp > 0;
    }

  n = tree->bst_alloc->libavl_malloc (tree->bst_alloc, sizeof *p);
  if (n == NULL)
    return NULL;

  tree->bst_count++;
  n->bst_link[0] = n->bst_link[1] = NULL;
  n->bst_data = item;
  if (q != NULL)
    q->bst_link[dir] = n;
  else
    tree->bst_root = n;

  return &n->bst_data;
}

/* Inserts |item| into |table|.
   Returns |NULL| if |item| was successfully inserted
   or if a memory allocation error occurred.
   Otherwise, returns the duplicate item. */
void *
bst_insert (struct bst_table *table, void *item)
{
  void **p = bst_probe (table, item);
  return p == NULL || *p == item ? NULL : *p;
}

/* Inserts |item| into |table|, replacing any duplicate item.
   Returns |NULL| if |item| was inserted without replacing a duplicate,
   or if a memory allocation error occurred.
   Otherwise, returns the item that was replaced. */
void *
bst_replace (struct bst_table *table, void *item)
{
  void **p = bst_probe (table, item);
  if (p == NULL || *p == item)
    return NULL;
  else
    {
      void *r = *p;
      *p = item;
      return r;
    }
}

/* Deletes from |tree| and returns an item matching |item|.
   Returns a null pointer if no matching item found. */
void *
bst_delete (struct bst_table *tree, const void *item)
{
  struct bst_node *p, *q; /* Node to delete and its parent. */
  int cmp;                /* Comparison between |p->bst_data| and |item|. */
  int dir;                /* Side of |q| on which |p| is located. */

  assert (tree != NULL && item != NULL);

  p = (struct bst_node *) &tree->bst_root;
  for (cmp = -1; cmp != 0;
       cmp = tree->bst_compare (item, p->bst_data, tree->bst_param))
    {
      dir = cmp > 0;
      q = p;
      p = p->bst_link[dir];
      if (p == NULL)
        return NULL;
    }
  item = p->bst_data;

  if (p->bst_link[1] == NULL)
    q->bst_link[dir] = p->bst_link[0];
  else
    {
      struct bst_node *r = p->bst_link[1];
      if (r->bst_link[0] == NULL)
        {
          r->bst_link[0] = p->bst_link[0];
          q->bst_link[dir] = r;
        }
      else
        {
          struct bst_node *s;
          for (;;)
            {
              s = r->bst_link[0];
              if (s->bst_link[0] == NULL)
                break;

              r = s;
            }
          r->bst_link[0] = s->bst_link[1];
          s->bst_link[0] = p->bst_link[0];
          s->bst_link[1] = p->bst_link[1];
          q->bst_link[dir] = s;
        }
    }

  tree->bst_alloc->libavl_free (tree->bst_alloc, p);
  tree->bst_count--;
  tree->bst_generation++;
  return (void *) item;
}

/* Refreshes the stack of parent pointers in |trav|
   and updates its generation number. */
static void
trav_refresh (struct bst_traverser *trav)
{
  assert (trav != NULL);

  trav->bst_generation = trav->bst_table->bst_generation;

  if (trav->bst_node != NULL)
    {
      bst_comparison_func *cmp = trav->bst_table->bst_compare;
      void *param = trav->bst_table->bst_param;
      struct bst_node *node = trav->bst_node;
      struct bst_node *i;

      trav->bst_height = 0;
      for (i = trav->bst_table->bst_root; i != node; )
        {
          assert (trav->bst_height < BST_MAX_HEIGHT);
          assert (i != NULL);

          trav->bst_stack[trav->bst_height++] = i;
          i = i->bst_link[cmp (node->bst_data, i->bst_data, param) > 0];
        }
    }
}

/* Initializes |trav| for use with |tree|
   and selects the null node. */
void
bst_t_init (struct bst_traverser *trav, struct bst_table *tree)
{
  trav->bst_table = tree;
  trav->bst_node = NULL;
  trav->bst_height = 0;
  trav->bst_generation = tree->bst_generation;
}

/* Initializes |trav| for |tree|
   and selects and returns a pointer to its least-valued item.
   Returns |NULL| if |tree| contains no nodes. */
void *
bst_t_first (struct bst_traverser *trav, struct bst_table *tree)
{
  struct bst_node *x;

  assert (tree != NULL && trav != NULL);

  trav->bst_table = tree;
  trav->bst_height = 0;
  trav->bst_generation = tree->bst_generation;

  x = tree->bst_root;
  if (x != NULL)
    while (x->bst_link[0] != NULL)
      {
        if (trav->bst_height >= BST_MAX_HEIGHT)
          {
            bst_balance (tree);
            return bst_t_first (trav, tree);
          }

        trav->bst_stack[trav->bst_height++] = x;
        x = x->bst_link[0];
      }
  trav->bst_node = x;

  return x != NULL ? x->bst_data : NULL;
}

/* Initializes |trav| for |tree|
   and selects and returns a pointer to its greatest-valued item.
   Returns |NULL| if |tree| contains no nodes. */
void *
bst_t_last (struct bst_traverser *trav, struct bst_table *tree)
{
  struct bst_node *x;

  assert (tree != NULL && trav != NULL);

  trav->bst_table = tree;
  trav->bst_height = 0;
  trav->bst_generation = tree->bst_generation;

  x = tree->bst_root;
  if (x != NULL)
    while (x->bst_link[1] != NULL)
      {
        if (trav->bst_height >= BST_MAX_HEIGHT)
          {
            bst_balance (tree);
            return bst_t_last (trav, tree);
          }

        trav->bst_stack[trav->bst_height++] = x;
        x = x->bst_link[1];
      }
  trav->bst_node = x;

  return x != NULL ? x->bst_data : NULL;
}

/* Searches for |item| in |tree|.
   If found, initializes |trav| to the item found and returns the item
   as well.
   If there is no matching item, initializes |trav| to the null item
   and returns |NULL|. */
void *
bst_t_find (struct bst_traverser *trav, struct bst_table *tree, void *item)
{
  struct bst_node *p, *q;

  assert (trav != NULL && tree != NULL && item != NULL);
  trav->bst_table = tree;
  trav->bst_height = 0;
  trav->bst_generation = tree->bst_generation;
  for (p = tree->bst_root; p != NULL; p = q)
    {
      int cmp = tree->bst_compare (item, p->bst_data, tree->bst_param);

      if (cmp < 0)
        q = p->bst_link[0];
      else if (cmp > 0)
        q = p->bst_link[1];
      else /* |cmp == 0| */
        {
          trav->bst_node = p;
          return p->bst_data;
        }

      if (trav->bst_height >= BST_MAX_HEIGHT)
        {
          bst_balance (trav->bst_table);
          return bst_t_find (trav, tree, item);
        }
      trav->bst_stack[trav->bst_height++] = p;
    }

  trav->bst_height = 0;
  trav->bst_node = NULL;
  return NULL;
}

/* Attempts to insert |item| into |tree|.
   If |item| is inserted successfully, it is returned and |trav| is
   initialized to its location.
   If a duplicate is found, it is returned and |trav| is initialized to
   its location.  No replacement of the item occurs.
   If a memory allocation failure occurs, |NULL| is returned and |trav|
   is initialized to the null item. */
void *
bst_t_insert (struct bst_traverser *trav, struct bst_table *tree, void *item)
{
  struct bst_node **q;

  assert (tree != NULL && item != NULL);

  trav->bst_table = tree;
  trav->bst_height = 0;

  q = &tree->bst_root;
  while (*q != NULL)
    {
      int cmp = tree->bst_compare (item, (*q)->bst_data, tree->bst_param);
      if (cmp == 0)
        {
          trav->bst_node = *q;
          trav->bst_generation = tree->bst_generation;
          return (*q)->bst_data;
        }

      if (trav->bst_height >= BST_MAX_HEIGHT)
        {
          bst_balance (tree);
          return bst_t_insert (trav, tree, item);
        }
      trav->bst_stack[trav->bst_height++] = *q;

      q = &(*q)->bst_link[cmp > 0];
    }

  trav->bst_node = *q = tree->bst_alloc->libavl_malloc (tree->bst_alloc,
                                                        sizeof **q);
  if (*q == NULL)
    {
      trav->bst_node = NULL;
      trav->bst_generation = tree->bst_generation;
      return NULL;
    }

  (*q)->bst_link[0] = (*q)->bst_link[1] = NULL;
  (*q)->bst_data = item;
  tree->bst_count++;
  trav->bst_generation = tree->bst_generation;
  return (*q)->bst_data;
}

/* Initializes |trav| to have the same current node as |src|. */
void *
bst_t_copy (struct bst_traverser *trav, const struct bst_traverser *src)
{
  assert (trav != NULL && src != NULL);

  if (trav != src)
    {
      trav->bst_table = src->bst_table;
      trav->bst_node = src->bst_node;
      trav->bst_generation = src->bst_generation;
      if (trav->bst_generation == trav->bst_table->bst_generation)
        {
          trav->bst_height = src->bst_height;
          memcpy (trav->bst_stack, (const void *) src->bst_stack,
                  sizeof *trav->bst_stack * trav->bst_height);
        }
    }

  return trav->bst_node != NULL ? trav->bst_node->bst_data : NULL;
}

/* Returns the next data item in inorder
   within the tree being traversed with |trav|,
   or if there are no more data items returns |NULL|. */
void *
bst_t_next (struct bst_traverser *trav)
{
  struct bst_node *x;

  assert (trav != NULL);

  if (trav->bst_generation != trav->bst_table->bst_generation)
    trav_refresh (trav);

  x = trav->bst_node;
  if (x == NULL)
    {
      return bst_t_first (trav, trav->bst_table);
    }
  else if (x->bst_link[1] != NULL)
    {
      if (trav->bst_height >= BST_MAX_HEIGHT)
        {
          bst_balance (trav->bst_table);
          return bst_t_next (trav);
        }

      trav->bst_stack[trav->bst_height++] = x;
      x = x->bst_link[1];

      while (x->bst_link[0] != NULL)
        {
          if (trav->bst_height >= BST_MAX_HEIGHT)
            {
              bst_balance (trav->bst_table);
              return bst_t_next (trav);
            }

          trav->bst_stack[trav->bst_height++] = x;
          x = x->bst_link[0];
        }
    }
  else
    {
      struct bst_node *y;

      do
        {
          if (trav->bst_height == 0)
            {
              trav->bst_node = NULL;
              return NULL;
            }

          y = x;
          x = trav->bst_stack[--trav->bst_height];
        }
      while (y == x->bst_link[1]);
    }
  trav->bst_node = x;

  return x->bst_data;
}

/* Returns the previous data item in inorder
   within the tree being traversed with |trav|,
   or if there are no more data items returns |NULL|. */
void *
bst_t_prev (struct bst_traverser *trav)
{
  struct bst_node *x;

  assert (trav != NULL);

  if (trav->bst_generation != trav->bst_table->bst_generation)
    trav_refresh (trav);

  x = trav->bst_node;
  if (x == NULL)
    {
      return bst_t_last (trav, trav->bst_table);
    }
  else if (x->bst_link[0] != NULL)
    {
      if (trav->bst_height >= BST_MAX_HEIGHT)
        {
          bst_balance (trav->bst_table);
          return bst_t_prev (trav);
        }

      trav->bst_stack[trav->bst_height++] = x;
      x = x->bst_link[0];

      while (x->bst_link[1] != NULL)
        {
          if (trav->bst_height >= BST_MAX_HEIGHT)
            {
              bst_balance (trav->bst_table);
              return bst_t_prev (trav);
            }

          trav->bst_stack[trav->bst_height++] = x;
          x = x->bst_link[1];
        }
    }
  else
    {
      struct bst_node *y;

      do
        {
          if (trav->bst_height == 0)
            {
              trav->bst_node = NULL;
              return NULL;
            }

          y = x;
          x = trav->bst_stack[--trav->bst_height];
        }
      while (y == x->bst_link[0]);
    }
  trav->bst_node = x;

  return x->bst_data;
}

/* Returns |trav|'s current item. */
void *
bst_t_cur (struct bst_traverser *trav)
{
  assert (trav != NULL);

  return trav->bst_node != NULL ? trav->bst_node->bst_data : NULL;
}

/* Replaces the current item in |trav| by |new| and returns the item replaced.
   |trav| must not have the null item selected.
   The new item must not upset the ordering of the tree. */
void *
bst_t_replace (struct bst_traverser *trav, void *new)
{
  void *old;

  assert (trav != NULL && trav->bst_node != NULL && new != NULL);
  old = trav->bst_node->bst_data;
  trav->bst_node->bst_data = new;
  return old;
}

/* Destroys |new| with |bst_destroy (new, destroy)|,
   first setting right links of nodes in |stack| within |new|
   to null pointers to avoid touching uninitialized data. */
static void
copy_error_recovery (struct bst_node **stack, int height,
                     struct bst_table *new, bst_item_func *destroy)
{
  assert (stack != NULL && height >= 0 && new != NULL);

  for (; height > 2; height -= 2)
    stack[height - 1]->bst_link[1] = NULL;
  bst_destroy (new, destroy);
}

/* Copies |org| to a newly created tree, which is returned.
   If |copy != NULL|, each data item in |org| is first passed to |copy|,
   and the return values are inserted into the tree,
   with |NULL| return values taken as indications of failure.
   On failure, destroys the partially created new tree,
   applying |destroy|, if non-null, to each item in the new tree so far,
   and returns |NULL|.
   If |allocator != NULL|, it is used for allocation in the new tree.
   Otherwise, the same allocator used for |org| is used. */
struct bst_table *
bst_copy (const struct bst_table *org, bst_copy_func *copy,
          bst_item_func *destroy, struct libavl_allocator *allocator)
{
  struct bst_node *stack[2 * (BST_MAX_HEIGHT + 1)];
  int height = 0;

  struct bst_table *new;
  const struct bst_node *x;
  struct bst_node *y;

  assert (org != NULL);
  new = bst_create (org->bst_compare, org->bst_param,
                    allocator != NULL ? allocator : org->bst_alloc);
  if (new == NULL)
    return NULL;
  new->bst_count = org->bst_count;
  if (new->bst_count == 0)
    return new;

  x = (const struct bst_node *) &org->bst_root;
  y = (struct bst_node *) &new->bst_root;
  for (;;)
    {
      while (x->bst_link[0] != NULL)
        {
          if (height >= 2 * (BST_MAX_HEIGHT + 1))
            {
              y->bst_data = NULL;
              y->bst_link[0] = y->bst_link[1] = NULL;
              copy_error_recovery (stack, height, new, destroy);

              bst_balance ((struct bst_table *) org);
              return bst_copy (org, copy, destroy, allocator);
            }

          y->bst_link[0] =
            new->bst_alloc->libavl_malloc (new->bst_alloc,
                                           sizeof *y->bst_link[0]);
          if (y->bst_link[0] == NULL)
            {
              if (y != (struct bst_node *) &new->bst_root)
                {
                  y->bst_data = NULL;
                  y->bst_link[1] = NULL;
                }

              copy_error_recovery (stack, height, new, destroy);
              return NULL;
            }

          stack[height++] = (struct bst_node *) x;
          stack[height++] = y;
          x = x->bst_link[0];
          y = y->bst_link[0];
        }
      y->bst_link[0] = NULL;

      for (;;)
        {
          if (copy == NULL)
            y->bst_data = x->bst_data;
          else
            {
              y->bst_data = copy (x->bst_data, org->bst_param);
              if (y->bst_data == NULL)
                {
                  y->bst_link[1] = NULL;
                  copy_error_recovery (stack, height, new, destroy);
                  return NULL;
                }
            }

          if (x->bst_link[1] != NULL)
            {
              y->bst_link[1] =
                new->bst_alloc->libavl_malloc (new->bst_alloc,
                                               sizeof *y->bst_link[1]);
              if (y->bst_link[1] == NULL)
                {
                  copy_error_recovery (stack, height, new, destroy);
                  return NULL;
                }

              x = x->bst_link[1];
              y = y->bst_link[1];
              break;
            }
          else
            y->bst_link[1] = NULL;

          if (height <= 2)
            return new;

          y = stack[--height];
          x = stack[--height];
        }
    }
}

/* Frees storage allocated for |tree|.
   If |destroy != NULL|, applies it to each data item in inorder. */
void
bst_destroy (struct bst_table *tree, bst_item_func *destroy)
{
  struct bst_node *p, *q;

  assert (tree != NULL);

  for (p = tree->bst_root; p != NULL; p = q)
    if (p->bst_link[0] == NULL)
      {
        q = p->bst_link[1];
        if (destroy != NULL && p->bst_data != NULL)
          destroy (p->bst_data, tree->bst_param);
        tree->bst_alloc->libavl_free (tree->bst_alloc, p);
      }
    else
      {
        q = p->bst_link[0];
        p->bst_link[0] = q->bst_link[1];
        q->bst_link[1] = p;
      }

  tree->bst_alloc->libavl_free (tree->bst_alloc, tree);
}

/* Converts |tree| into a vine. */
static void
tree_to_vine (struct bst_table *tree)
{
  struct bst_node *q, *p;

  q = (struct bst_node *) &tree->bst_root;
  p = tree->bst_root;
  while (p != NULL)
    if (p->bst_link[1] == NULL)
      {
        q = p;
        p = p->bst_link[0];
      }
    else
      {
        struct bst_node *r = p->bst_link[1];
        p->bst_link[1] = r->bst_link[0];
        r->bst_link[0] = p;
        p = r;
        q->bst_link[0] = r;
      }
}

/* Performs a compression transformation |count| times,
   starting at |root|. */
static void
compress (struct bst_node *root, unsigned long count)
{
  assert (root != NULL);

  while (count--)
    {
      struct bst_node *red = root->bst_link[0];
      struct bst_node *black = red->bst_link[0];

      root->bst_link[0] = black;
      red->bst_link[0] = black->bst_link[1];
      black->bst_link[1] = red;
      root = black;
    }
}

/* Converts |tree|, which must be in the shape of a vine, into a balanced
   tree. */
static void
vine_to_tree (struct bst_table *tree)
{
  unsigned long vine;   /* Number of nodes in main vine. */
  unsigned long leaves; /* Nodes in incomplete bottom level, if any. */
  int height;           /* Height of produced balanced tree. */

  leaves = tree->bst_count + 1;
  for (;;)
    {
      unsigned long next = leaves & (leaves - 1);
      if (next == 0)
        break;
      leaves = next;
    }
  leaves = tree->bst_count + 1 - leaves;

  compress ((struct bst_node *) &tree->bst_root, leaves);

  vine = tree->bst_count - leaves;
  height = 1 + (leaves > 0);
  while (vine > 1)
    {
      compress ((struct bst_node *) &tree->bst_root, vine / 2);
      vine /= 2;
      height++;
    }

  if (height > BST_MAX_HEIGHT)
    {
      fprintf (stderr, "libavl: Tree too big (%lu nodes) to handle.",
               (unsigned long) tree->bst_count);
      exit (EXIT_FAILURE);
    }
}

/* Balances |tree|.
   Ensures that no simple path from the root to a leaf has more than
   |BST_MAX_HEIGHT| nodes. */
void
bst_balance (struct bst_table *tree)
{
  assert (tree != NULL);

  tree_to_vine (tree);
  vine_to_tree (tree);
  tree->bst_generation++;
}

/* Allocates |size| bytes of space using |malloc()|.
   Returns a null pointer if allocation fails. */
void *
bst_malloc (struct libavl_allocator *allocator, size_t size)
{
  assert (allocator != NULL && size > 0);
  return malloc (size);
}

/* Frees |block|. */
void
bst_free (struct libavl_allocator *allocator, void *block)
{
  assert (allocator != NULL && block != NULL);
  free (block);
}

/* Default memory allocator that uses |malloc()| and |free()|. */
struct libavl_allocator bst_allocator_default =
  {
    bst_malloc,
    bst_free
  };

#undef NDEBUG
#include <assert.h>

/* Asserts that |bst_insert()| succeeds at inserting |item| into |table|. */
void
(bst_assert_insert) (struct bst_table *table, void *item)
{
  void **p = bst_probe (table, item);
  assert (p != NULL && *p == item);
}

/* Asserts that |bst_delete()| really removes |item| from |table|,
   and returns the removed item. */
void *
(bst_assert_delete) (struct bst_table *table, void *item)
{
  void *p = bst_delete (table, item);
  assert (p != NULL);
  return p;
}

