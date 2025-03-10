/*
 * cache.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "cache.h"
#include "main.h"

/* cache configuration parameters */
static int cache_split = 0;
static int cache_usize = DEFAULT_CACHE_SIZE;
static int cache_isize = DEFAULT_CACHE_SIZE;
static int cache_dsize = DEFAULT_CACHE_SIZE;
static int cache_block_size = DEFAULT_CACHE_BLOCK_SIZE;
static int words_per_block = DEFAULT_CACHE_BLOCK_SIZE / WORD_SIZE;
static int cache_assoc = DEFAULT_CACHE_ASSOC;
static int cache_writeback = DEFAULT_CACHE_WRITEBACK;
static int cache_writealloc = DEFAULT_CACHE_WRITEALLOC;

/* cache model data structures */
static Pcache icache;
static Pcache dcache;
static cache c1;
static cache c2;
static cache_stat cache_stat_inst;
static cache_stat cache_stat_data;

/************************************************************/
void set_cache_param(param, value) int param;
int value;
{

  switch (param)
  {
  case CACHE_PARAM_BLOCK_SIZE:
    cache_block_size = value;
    words_per_block = value / WORD_SIZE;
    break;
  case CACHE_PARAM_USIZE:
    cache_split = FALSE;
    cache_usize = value;
    break;
  case CACHE_PARAM_ISIZE:
    cache_split = TRUE;
    cache_isize = value;
    break;
  case CACHE_PARAM_DSIZE:
    cache_split = TRUE;
    cache_dsize = value;
    break;
  case CACHE_PARAM_ASSOC:
    cache_assoc = value;
    break;
  case CACHE_PARAM_WRITEBACK:
    cache_writeback = TRUE;
    break;
  case CACHE_PARAM_WRITETHROUGH:
    cache_writeback = FALSE;
    break;
  case CACHE_PARAM_WRITEALLOC:
    cache_writealloc = TRUE;
    break;
  case CACHE_PARAM_NOWRITEALLOC:
    cache_writealloc = FALSE;
    break;
  default:
    printf("error set_cache_param: bad parameter value\n");
    exit(-1);
  }
}
/************************************************************/

/************************************************************/
void init_cache()
{

  /* Instruction cache statistics */
  cache_stat_inst.accesses = 0;     /* number of memory references */
  cache_stat_inst.misses = 0;        /* number of cache misses */
  cache_stat_inst.replacements = 0;  /* number of misses that cause replacments */
  cache_stat_inst.demand_fetches = 0; /* number of fetches */
  cache_stat_inst.copies_back = 0;    /* number of write backs */

  /* Data cache statistics */
  cache_stat_data.accesses = 0;
  cache_stat_data.misses = 0;
  cache_stat_data.replacements = 0;
  cache_stat_data.demand_fetches = 0;
  cache_stat_data.copies_back = 0;

  /* Unified case, I'll use c1 as the unified one */
  if (cache_split == 0)
  {
    c1.size = cache_usize;
    c1.associativity = cache_assoc;
    c1.n_sets = cache_usize / (cache_assoc * cache_block_size);
    c1.index_mask_offset = (int)LOG2(cache_block_size);
    c1.index_mask = (c1.n_sets - 1) << c1.index_mask_offset; /* (addr & index_mask) >> index_mask_offset would show the index bits */
    /* I could get the tag with (addr) >> (c1.index_mask_offset + LOG2(c1.n_sets)) which is a right shift in the address by the number of index and offset bits */
    c1.LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line) * c1.n_sets);
    c1.LRU_tail = (Pcache_line *)malloc(sizeof(Pcache_line) * c1.n_sets);
    c1.set_contents = (int *)malloc(sizeof(int) * c1.n_sets);

    /* We also need to initialize the LRU structure to NULL's and the contents of the cache to 0*/
    for (int i = 0; i < c1.n_sets; i++)
    {
      c1.LRU_head[i] = NULL;
      c1.LRU_tail[i] = NULL;
      c1.set_contents[i] = 0;
    }
    c1.contents = 0;
  }
  else
  { /* split cache, c1 for instructions using cache_isize, and c2 for data using cache_dsize*/

    /* Instruction cache */
    c1.size = cache_isize;
    c1.associativity = cache_assoc;
    c1.n_sets = cache_isize / (cache_assoc * cache_block_size);
    c1.index_mask_offset = (int)LOG2(cache_block_size);
    c1.index_mask = (c1.n_sets - 1) << c1.index_mask_offset;
    c1.LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line) * c1.n_sets);
    c1.LRU_tail = (Pcache_line *)malloc(sizeof(Pcache_line) * c1.n_sets);
    c1.set_contents = (int *)malloc(sizeof(int) * c1.n_sets);
    for (int i = 0; i < c1.n_sets; i++)
    {
      c1.LRU_head[i] = NULL;
      c1.LRU_tail[i] = NULL;
      c1.set_contents[i] = 0;
    }
    c1.contents = 0;

    /* Data cache*/
    c2.size = cache_dsize;
    c2.associativity = cache_assoc;
    c2.n_sets = cache_dsize / (cache_assoc * cache_block_size);
    c2.index_mask_offset = (int)LOG2(cache_block_size);
    c2.index_mask = (c2.n_sets - 1) << c2.index_mask_offset;
    c2.LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line) * c2.n_sets);
    c2.LRU_tail = (Pcache_line *)malloc(sizeof(Pcache_line) * c2.n_sets);
    c2.set_contents = (int *)malloc(sizeof(int) * c2.n_sets);
    for (int i = 0; i < c2.n_sets; i++)
    {
      c2.LRU_head[i] = NULL;
      c2.LRU_tail[i] = NULL;
      c2.set_contents[i] = 0;
    }
    c2.contents = 0;
  }
}
/************************************************************/

/************************************************************/
void perform_access(unsigned addr, unsigned access_type)
{
  unsigned words_in_block;

  if (cache_split == 0)
  { /* Unified cache case */
    words_in_block = (c1.size > 0) ? (cache_block_size / WORD_SIZE) : 0;

    /* Register the access */
    if (access_type == TRACE_INST_LOAD)
      cache_stat_inst.accesses++;
    else
      cache_stat_data.accesses++;

    /* getting the tag, index, and offset */
    unsigned tag = addr >> (c1.index_mask_offset + LOG2(c1.n_sets));
    unsigned index = (addr & c1.index_mask) >> c1.index_mask_offset;
    unsigned offset = addr & (cache_block_size - 1);

    if (c1.associativity == 1)
    { /* Direct-mapped unified cache case*/
      Pcache_line line = c1.LRU_head[index];
      if (line != NULL && line->tag == tag)
      { /* cache hit case */
        if (access_type == TRACE_DATA_STORE)
        {
          if (cache_writeback)
            line->dirty = 1;
          else
            cache_stat_data.copies_back += 1;
        }
        delete (&c1.LRU_head[index], &c1.LRU_tail[index], line);
        insert(&c1.LRU_head[index], &c1.LRU_tail[index], line);
      }
      else
      { /*cache miss case */
        if (access_type == TRACE_INST_LOAD)
        {
          cache_stat_inst.misses++;
          cache_stat_inst.demand_fetches += words_in_block;
        }
        else
        { /* data store miss */
          if (cache_writealloc)
          {
            cache_stat_data.misses++;
            cache_stat_data.demand_fetches += words_in_block;
          }
          else
          {
            cache_stat_data.misses++;
            if (cache_writeback)
              cache_stat_data.copies_back += words_in_block;
            else
              cache_stat_data.copies_back += 1;
            return;
          }
        }
        if (line != NULL)
        {
          if (line->dirty == 1 && cache_writeback)
            cache_stat_data.copies_back += words_in_block;
          if (access_type == TRACE_INST_LOAD)
            cache_stat_inst.replacements++;
          else
            cache_stat_data.replacements++;
          delete (&c1.LRU_head[index], &c1.LRU_tail[index], line);
          free(line);
        }
        Pcache_line new_line = (Pcache_line)malloc(sizeof(cache_line));
        new_line->tag = tag;
        new_line->dirty = (access_type == TRACE_DATA_STORE) ? (cache_writeback ? 1 : 0) : 0;
        new_line->LRU_next = NULL;
        new_line->LRU_prev = NULL;
        insert(&c1.LRU_head[index], &c1.LRU_tail[index], new_line);
        c1.set_contents[index] = 1;
      }
    }
    else
    { /* set-associative unified cachecase */
      Pcache_line curr = c1.LRU_head[index];
      Pcache_line hit_line = NULL;
      while (curr != NULL)
      {
        if (curr->tag == tag)
        {
          hit_line = curr;
          break;
        }
        curr = curr->LRU_next;
      }
      if (hit_line != NULL)
      { /* Cache hit */
        if (access_type == TRACE_DATA_STORE)
        {
          if (cache_writeback)
            hit_line->dirty = 1;
          else
            cache_stat_data.copies_back += 1;
        }
        delete (&c1.LRU_head[index], &c1.LRU_tail[index], hit_line);
        insert(&c1.LRU_head[index], &c1.LRU_tail[index], hit_line);
      }
      else
      { /* cache miss case */
        if (access_type == TRACE_INST_LOAD)
        {
          cache_stat_inst.misses++;
          cache_stat_inst.demand_fetches += words_in_block;
        }
        else
        {
          if (cache_writealloc)
          {
            cache_stat_data.misses++;
            cache_stat_data.demand_fetches += words_in_block;
            if (!cache_writeback)
              cache_stat_data.copies_back += 1;
          }
          else
          {
            cache_stat_data.misses++;
            if (cache_writeback)
              cache_stat_data.copies_back += words_in_block;
            else
              cache_stat_data.copies_back += 1;
            return;
          }
        }
        if (c1.set_contents[index] >= c1.associativity)
        {
          /* LRU eviction from tail */
          Pcache_line victim = c1.LRU_tail[index];
          if (victim != NULL)
          {
            if (victim->dirty == 1 && cache_writeback)
              cache_stat_data.copies_back += words_in_block;
            if (access_type == TRACE_INST_LOAD)
              cache_stat_inst.replacements++;
            else
              cache_stat_data.replacements++;
            delete (&c1.LRU_head[index], &c1.LRU_tail[index], victim);
            free(victim);
          }
        }
        else
        {
          c1.set_contents[index]++;
        }
        Pcache_line new_line = (Pcache_line)malloc(sizeof(cache_line));
        new_line->tag = tag;
        new_line->dirty = (access_type == TRACE_DATA_STORE) ? (cache_writeback ? 1 : 0) : 0;
        new_line->LRU_next = NULL;
        new_line->LRU_prev = NULL;
        insert(&c1.LRU_head[index], &c1.LRU_tail[index], new_line);
      }
    }
  }
  
  else // splitt caches case
  {
    cache *target;
    cache_stat *target_stat;
    unsigned local_block_size = cache_block_size;
    // write policies
    int wb_local = cache_writeback;
    int wa_local = cache_writealloc;

    if (access_type == TRACE_INST_LOAD)
    {
      target = &c1; // c1 for instruction cache
      target_stat = &cache_stat_inst;
    }
    else
    {
      target = &c2; // c2 for the data cache
      target_stat = &cache_stat_data;
    }
    target_stat->accesses++;

    unsigned tag = addr >> (target->index_mask_offset + LOG2(target->n_sets));
    unsigned index = (addr & target->index_mask) >> target->index_mask_offset;
    unsigned local_words_in_block = local_block_size / WORD_SIZE;

    if (target->associativity == 1)
    { /* Direct-mapped split cache */
      Pcache_line line = target->LRU_head[index];
      if (line != NULL && line->tag == tag)
      {
        if (access_type == TRACE_DATA_STORE)
        {
          if (wb_local)
            line->dirty = 1;
          else
            target_stat->copies_back += 1;
        }
        delete (&target->LRU_head[index], &target->LRU_tail[index], line);
        insert(&target->LRU_head[index], &target->LRU_tail[index], line);
      }
      else
      {
        if (access_type == TRACE_INST_LOAD)
        {
          target_stat->misses++;
          target_stat->demand_fetches += local_words_in_block;
        }
        else
        {
          if (wa_local)
          {
            target_stat->misses++;
            target_stat->demand_fetches += local_words_in_block;
            if (!wb_local)
              target_stat->copies_back += 1;
          }
          else
          {
            target_stat->misses++;
            if (wb_local)
              target_stat->copies_back += local_words_in_block;
            else
              target_stat->copies_back += 1;
            ;
          }
        }
        if (line != NULL)
        {
          if (line->dirty && wb_local)
            target_stat->copies_back += local_words_in_block;
          if (access_type == TRACE_INST_LOAD)
            target_stat->replacements++;
          else
            target_stat->replacements++;
          delete (&target->LRU_head[index], &target->LRU_tail[index], line);
          free(line);
        }
        {
          Pcache_line new_line = (Pcache_line)malloc(sizeof(cache_line));
          new_line->tag = tag;
          new_line->dirty = (access_type == TRACE_DATA_STORE) ? (wa_local ? (wb_local ? 1 : 0) : 0) : 0;
          new_line->LRU_next = NULL;
          new_line->LRU_prev = NULL;
          insert(&target->LRU_head[index], &target->LRU_tail[index], new_line);
          target->set_contents[index] = 1;
        }
      }
    }
    else
    { /* set-associative split cache */
      Pcache_line curr = target->LRU_head[index];
      Pcache_line hit_line = NULL;
      while (curr != NULL)
      {
        if (curr->tag == tag)
        {
          hit_line = curr;
          break;
        }
        curr = curr->LRU_next;
      }
      if (hit_line != NULL)
      { /* cache hitcase */
        if (access_type == TRACE_DATA_STORE)
        {
          if (wb_local)
            hit_line->dirty = 1;
          else
            target_stat->copies_back += 1;
        }
        delete (&target->LRU_head[index], &target->LRU_tail[index], hit_line);
        insert(&target->LRU_head[index], &target->LRU_tail[index], hit_line);
      }
      else
      { /* cache miss case */
        if (access_type == TRACE_INST_LOAD)
        {
          target_stat->misses++;
          target_stat->demand_fetches += local_words_in_block;
        }
        else
        {
          if (wa_local)
          {
            target_stat->misses++;
            target_stat->demand_fetches += local_words_in_block;
            if (!wb_local)
              target_stat->copies_back += 1;
          }
          else
          {
            target_stat->misses++;
            if (wb_local)
              target_stat->copies_back += local_words_in_block;
            else
              target_stat->copies_back += 1;
          }
        }
        if (target->set_contents[index] >= target->associativity)
        {
          Pcache_line victim = target->LRU_tail[index]; 
          if (victim != NULL)
          {
            if (victim->dirty && wb_local)
              target_stat->copies_back += local_words_in_block;
            target_stat->replacements++;
            delete (&target->LRU_head[index], &target->LRU_tail[index], victim);
            free(victim);
          }
        }
        else
        {
          target->set_contents[index]++;
        }
        {
          Pcache_line new_line = (Pcache_line)malloc(sizeof(cache_line));
          new_line->tag = tag;
          new_line->dirty = (access_type == TRACE_DATA_STORE) ? (wa_local ? (wb_local ? 1 : 0) : 0) : 0;
          new_line->LRU_next = NULL;
          new_line->LRU_prev = NULL;
          insert(&target->LRU_head[index], &target->LRU_tail[index], new_line);
        }
      }
    }
  }
}

/************************************************************/

/************************************************************/
void flush()
{
  unsigned words_in_block = c1.size > 0 ? (unsigned)(cache_block_size / WORD_SIZE) : 0;

  /* flush the cache */
  if (cache_split == 0)
  { /* for unified case flush remaining dirty bits and record statistic of copies back, also clean cache*/
    for (int i = 0; i < c1.n_sets; i++)
    {
      Pcache_line current = c1.LRU_head[i];
      while (current != NULL)
      {
        if (cache_writeback && current->dirty == 1)
        {
          cache_stat_data.copies_back += words_in_block;
        }
        Pcache_line temp = current;
        current = current->LRU_next;
        free(temp);
      }
      c1.LRU_head[i] = NULL;
      c1.LRU_tail[i] = NULL;
      c1.set_contents[i] = 0;
    }
  }
  else
  { /* split mode */
    for (int i = 0; i < c1.n_sets; i++)
    {
      Pcache_line current = c1.LRU_head[i];
      while (current != NULL)
      {
        if (cache_writeback && current->dirty == 1)
        {
          cache_stat_inst.copies_back += words_in_block;
        }
        Pcache_line temp = current;
        current = current->LRU_next;
        free(temp);
      }
      c1.LRU_head[i] = NULL;
      c1.LRU_tail[i] = NULL;
      c1.set_contents[i] = 0;
    }

    for (int i = 0; i < c2.n_sets; i++)
    {
      Pcache_line current = c2.LRU_head[i];
      while (current != NULL)
      {
        if (cache_writeback && current->dirty == 1)
        {
          cache_stat_data.copies_back += words_in_block;
        }
        Pcache_line temp = current;
        current = current->LRU_next;
        free(temp);
      }
      c2.LRU_head[i] = NULL;
      c2.LRU_tail[i] = NULL;
      c2.set_contents[i] = 0;
    }
  }
}
/************************************************************/

/************************************************************/
void delete(head, tail, item)
    Pcache_line *head,
    *tail;
Pcache_line item;
{
  if (item->LRU_prev)
  {
    item->LRU_prev->LRU_next = item->LRU_next;
  }
  else
  {
    /* item at head */
    *head = item->LRU_next;
  }

  if (item->LRU_next)
  {
    item->LRU_next->LRU_prev = item->LRU_prev;
  }
  else
  {
    /* item at tail */
    *tail = item->LRU_prev;
  }
}
/************************************************************/

/************************************************************/
/* inserts at the head of the list */
void insert(head, tail, item)
    Pcache_line *head,
    *tail;
Pcache_line item;
{
  item->LRU_next = *head;
  item->LRU_prev = (Pcache_line)NULL;

  if (item->LRU_next)
    item->LRU_next->LRU_prev = item;
  else
    *tail = item;

  *head = item;
}
/************************************************************/

/************************************************************/
void dump_settings()
{
  printf("*** CACHE SETTINGS ***\n");
  if (cache_split)
  {
    printf("  Split I- D-cache\n");
    printf("  I-cache size: \t%d\n", cache_isize);
    printf("  D-cache size: \t%d\n", cache_dsize);
  }
  else
  {
    printf("  Unified I- D-cache\n");
    printf("  Size: \t%d\n", cache_usize);
  }
  printf("  Associativity: \t%d\n", cache_assoc);
  printf("  Block size: \t%d\n", cache_block_size);
  printf("  Write policy: \t%s\n",
         cache_writeback ? "WRITE BACK" : "WRITE THROUGH");
  printf("  Allocation policy: \t%s\n",
         cache_writealloc ? "WRITE ALLOCATE" : "WRITE NO ALLOCATE");
}
/************************************************************/

/************************************************************/
void print_stats()
{
  printf("\n*** CACHE STATISTICS ***\n");

  printf(" INSTRUCTIONS\n");
  printf("  accesses:  %d\n", cache_stat_inst.accesses);
  printf("  misses:    %d\n", cache_stat_inst.misses);
  if (!cache_stat_inst.accesses)
    printf("  miss rate: 0 (0)\n");
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n",
           (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses,
           1.0 - (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses);
  printf("  replace:   %d\n", cache_stat_inst.replacements);

  printf(" DATA\n");
  printf("  accesses:  %d\n", cache_stat_data.accesses);
  printf("  misses:    %d\n", cache_stat_data.misses);
  if (!cache_stat_data.accesses)
    printf("  miss rate: 0 (0)\n");
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n",
           (float)cache_stat_data.misses / (float)cache_stat_data.accesses,
           1.0 - (float)cache_stat_data.misses / (float)cache_stat_data.accesses);
  printf("  replace:   %d\n", cache_stat_data.replacements);

  printf(" TRAFFIC (in words)\n");
  printf("  demand fetch:  %d\n", cache_stat_inst.demand_fetches +
                                      cache_stat_data.demand_fetches);
  printf("  copies back:   %d\n", cache_stat_inst.copies_back +
                                      cache_stat_data.copies_back);
}
/************************************************************/
