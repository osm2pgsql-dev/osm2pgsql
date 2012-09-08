#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "osmtypes.h"
#include "binarysearcharray.h"

static int binary_search_lookup(struct binary_search_array * array, int key)
{
    int a = 0;
    int b = array->size - 1;
    while (a <= b)
    {
        int pivot = ((b - a) >> 1) + a;
        //fprintf(stderr, "testing pivot %i (%i, %i) which is %i against %i\n", pivot,a,b, array->array[pivot].key, key);
        if (array->array[pivot].key == key)
        {
            return pivot;
        }
        else if (array->array[pivot].key > key)
        {
            b = pivot - 1;
        }
        else
        {
            a = pivot + 1;
        }
    }
    if ((a < array->size) && (array->array[a].key < key))
        a++;
    return a | (1 << (sizeof(int) * 8 - 1));
}
osmid_t binary_search_get(struct binary_search_array * array, int key)
{
    if (array->size == 0)
        return -1;
    //fprintf(stderr,"!!Looking up binary search of %i\n", key);
    int idx = binary_search_lookup(array, key);
    //fprintf(stderr,"Looking up binary search of %i %i on size %i\n", key, idx, array->size);
    if (idx < 0)
    {
        return -1;
    }
    else
    {
        return array->array[idx].value;
    }
    exit(1);
}

void binary_search_remove(struct binary_search_array * array, int key)
{
    //fprintf(stderr,"!!REMOVING with binary search of %i\n", key);
    int idx = binary_search_lookup(array, key);
    if (idx < 0)
    {
        //fprintf(stderr,"**Was Not There %i\n", key);
        return;
    }
    else
    {
        memmove(&(array->array[idx]), &(array->array[idx + 1]),
                sizeof(struct key_val_tuple) * (array->capacity - idx - 1));
        array->size--;
    }
}
void binary_search_add(struct binary_search_array * array, int key,
        osmid_t value)
{
    int i, j;
    if (array->size < array->capacity)
    {
        //fprintf(stderr,"adding key value %i %i\n", key, value);
        if (array->size == 0)
        {
            array->array[0].key = key;
            array->array[0].value = value;
            array->size++;
            return;
        }
        int idx = binary_search_lookup(array, key);
        if (idx < 0)
        {
            idx = idx & (~(1 << (sizeof(int) * 8 - 1)));
            //fprintf(stderr, "Inserting %i at position %i %i\n", key, idx, (~(1 << (sizeof(int) * 8 - 1))));
            memmove(&(array->array[idx + 1]), &(array->array[idx]),
                    sizeof(struct key_val_tuple) * (array->capacity - idx - 1));
            array->array[idx].key = key;
            array->array[idx].value = value;
            array->size++;
        }
        else
        {
            fprintf(stderr, "dupplicate!\n");
            exit(1);
        }
    }
}

struct binary_search_array * init_search_array(int capacity)
{
    struct binary_search_array * array = calloc(1,
            sizeof(struct binary_search_array));
    array->array = calloc(capacity + 1, sizeof(struct key_val_tuple));
    if (!array->array) {
        fprintf(stderr, "Out of memory trying to allocate %li bytes for binary search array\n", ((capacity + 1) * sizeof(struct key_val_tuple)));
        exit_nicely();
    }
    array->capacity = capacity;
    array->size = 0;
    return array;
}

void shutdown_search_array(struct binary_search_array ** array)
{
    free((*array)->array);
    (*array)->array = NULL;
    (*array)->capacity = 0;
    free(*array);
    *array = NULL;
}
