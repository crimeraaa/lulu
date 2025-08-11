/*
** $Id: ltable.c,v 2.32.1.2 2007/12/28 15:32:23 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/


/*
** Implementation of tables (aka arrays, objects, or hash tables).
** Tables keep its elements in two parts: an array part and a hash part.
** Non-negative integer keys are all candidates to be kept in the array
** part. The actual size of the array is the largest `n' such that at
** least half the slots between 0 and n are in use.
** Hash uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the `original' position that its hash gives
** to it), then the colliding element is in its own main position.
** Hence even when the load factor reaches 100%, performance remains good.
*/

#include <math.h>
#include <string.h>

#define ltable_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"


/*
** max size of array part is 2^MAXBITS
*/
#if LUAI_BITSINT > 26
#define MAXBITS		26
#else
#define MAXBITS		(LUAI_BITSINT-2)
#endif

#define MAXASIZE	(1 << MAXBITS)


#define hashpow2(t,n)      (gnode(t, lmod((n), sizenode(t))))

#define hashstr(t,str)  hashpow2(t, (str)->tsv.hash)
#define hashboolean(t,p)        hashpow2(t, p)


/*
** for some types, it is better to avoid modulus by power of 2, as
** they tend to have many 2 factors.
*/
#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t) - 1) | 1))))


#define hashpointer(t,p)	hashmod(t, IntPoint(p))


/*
** number of ints inside a lua_Number
*/
#define numints		cast_int(sizeof(lua_Number)/sizeof(int))



#define dummynode		(&dummynode_)

static const Node dummynode_ = {
  {{NULL}, LUA_TNIL},  /* value */
  {{{NULL}, LUA_TNIL, NULL}}  /* key */
};


/*
** hash for lua_Numbers
*/
static Node *hashnum (const Table *t, lua_Number n) {
  unsigned int a[numints];
  int i;
  if (luai_numeq(n, 0))  /* avoid problems with -0 */
    return gnode(t, 0);
  memcpy(a, &n, sizeof(a));
  for (i = 1; i < numints; i++) a[0] += a[i];
  return hashmod(t, a[0]);
}



/*
** returns the `main' position of an element in a table (that is, the index
** of its hash value)
*/
static Node *mainposition (const Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNUMBER:
      return hashnum(t, nvalue(key));
    case LUA_TSTRING:
      return hashstr(t, rawtsvalue(key));
    case LUA_TBOOLEAN:
      return hashboolean(t, bvalue(key));
    case LUA_TLIGHTUSERDATA:
      return hashpointer(t, pvalue(key));
    default:
      return hashpointer(t, gcvalue(key));
  }
}


/*
** returns the index for `key' if `key' is an appropriate key to live in
** the array part of the table, -1 otherwise.
*/
static int arrayindex (const TValue *key) {
  if (ttisnumber(key)) {
    lua_Number n = nvalue(key);
    int k;
    lua_number2int(k, n);
    /* `n` can be represented as an `int` without loss? */
    if (luai_numeq(cast_num(k), n)) {
      return k;
    }
  }
  return -1;  /* `key' did not match some condition */
}


/*
** returns the index of a `key' for table traversals. First goes all
** elements in the array part, then elements in the hash part. The
** beginning of a traversal is signalled by -1.
*/
static int findindex (lua_State *L, Table *t, StkId key) {
  int i;
  if (ttisnil(key))
    return -1;  /* first iteration */
  i = arrayindex(key);
  if (0 < i && i <= t->sizearray)  /* is `key' inside array part? */
    return i-1;  /* yes; that's the index (corrected to C) */
  else {
    Node *n = mainposition(t, key);
    do {  /* check whether `key' is somewhere in the chain */
      /* key may be dead already, but it is ok to use it in `next' */
      if (luaO_rawequalObj(key2tval(n), key)
        || (ttype(gkey(n)) == LUA_TDEADKEY && iscollectable(key)
          && gcvalue(gkey(n)) == gcvalue(key))) {
        i = cast_int(n - gnode(t, 0));  /* key index in hash table */
        /* hash elements are numbered after array ones */
        return i + t->sizearray;
      }
      else n = gnext(n);
    } while (n);
    luaG_runerror(L, "invalid key to " LUA_QL("next"));  /* key not found */
    return 0;  /* to avoid warnings */
  }
}


bool luaH_next (lua_State *L, Table *t, StkId key) {
  int i = findindex(L, t, key);  /* find original element */
  for (i++; i < t->sizearray; i++) {  /* try first array part */
    if (!ttisnil(&t->array[i])) {  /* a non-nil value? */
      setnvalue(key, cast_num(i+1));
      setobj2s(L, key+1, &t->array[i]);
      return true;
    }
  }
  for (i -= t->sizearray; i < sizenode(t); i++) {  /* then hash part */
    Node *n = gnode(t, i);
    TValue *v = gval(n);
    if (!ttisnil(v)) {  /* a non-nil value? */
      TValue *k = key2tval(n);
      setobj2s(L, key, k);
      setobj2s(L, key+1, v);
      return true;
    }
  }
  return false;  /* no more elements */
}


/*
** {=============================================================
** Rehash
** ==============================================================
*/


static int computesizes (int nums[], int *n_array) {
  int bit;
  int pow2;  /* 2^bit */

  int a = 0;  /* total number of elements smaller than `pow2` */
  int n_array_active = 0;  /* number of elements to go to array part */
  int n_array_optimal = 0;

  /* if `*n_array == 0` then the loop is never entered. */
  for (bit = 0, pow2 = 1; (pow2 / 2) < *n_array; bit++, pow2 *= 2) {
    /* Number of (potentially) active elements in this index range. */
    int used = nums[bit];
    if (used > 0) {
      a += used;
      /* More than half of all possible array elements present at this point? */
      if (a > pow2 / 2) {
        /* Optimal size (up until now) */
        n_array_optimal = pow2;

        /* All elements smaller than `n_array_optimal` go to the array part.
          This is important when rehashing because we will move integer
          keys to the array if at all possible. */
        n_array_active = a;
      }
    }
    /* all elements already counted? */
    if (a == *n_array) {
      break;
    }
  }
  *n_array = n_array_optimal;
  lua_assert(*n_array / 2 <= n_array_active && n_array_active <= *n_array);
  return n_array_active;
}


/**
 * @brief
 *    Checks if `key` is a valid array index and fills in the appropriate
 *    range index in `nums` if so.
 *
 * @return
 *    1 if key is a potentially valid array index else 0.
 */
static int countint (const TValue *key, int nums[]) {
  int k = arrayindex(key);
  if (0 < k && k <= MAXASIZE) {  /* is `key' an appropriate array index? */
    /* Get the exponent of the start of our bit range. */
    int lg = ceillog2(k);
    nums[lg]++;  /* count as such */
    return 1;
  }
  else {
    return 0;
  }
}


/**
 * @param nums
 *    Helps track which ranges of indexes have how many active elements
 *    in the array.
 *
 * @return
 *    The number of active array elements. This does *not* represent
 *    the optimal size, as it does not consider holes in the array nor
 *    does it count integer keys in the hash part.
 */
static int numusearray (const Table *t, int nums[]) {
  int bit; /* exponent for power of 2 we are currently at. */
  int pow2; /* power of 2 we want: 2^bit */
  int n_array_used = 0;  /* summation of `nums' */
  int i = 1;  /* count to traverse all array keys */
  for (bit = 0, pow2 = 1;
    bit <= MAXBITS;
    bit++, pow2 *= 2) { /* for each slice */

    int used = 0;  /* counter for active array items for this range. */
    int lim = pow2; /* range end for this `i` */
    /* `lim` would read out of bounds? */
    if (lim > t->sizearray) {
      /* Clamp `lim`. */
      lim = t->sizearray;

      /* No more elements to count? E.g. `i == 1 && t->sizearray == 0`
        when dealing with empty arrays. */
      if (i > lim) {
        break;
      }
    }
    /* count elements in range (2^(bit - 1), 2^(bit)] */
    for (; i <= lim; i++) {
      TValue *v = &t->array[i - 1];
      if (!ttisnil(v)) {
        used++;
      }
    }
    nums[bit] += used;
    n_array_used += used;
  }
  return n_array_used;
}


/**
 * @param pnasize
 *    Points to a variable holding the current number of active elements
 *    in `t->array`.
 *
 * @return
 *    The number of active elements in the hash segment.
 */
static int numusehash (const Table *t, int nums[], int *n_array_used) {
  int totaluse = 0;  /* total number of elements */
  int n_array_extra = 0;  /* summation of `nums' */
  int i = sizenode(t);
  while (i--) {
    Node *n = &t->node[i];
    TValue *v = gval(n);
    if (!ttisnil(v)) {
      TValue *k = key2tval(n);

      /* Add this node to the array count if its key is an array index. */
      n_array_extra += countint(k, nums);

      /* count all used hash keys regardless if it could be in the array */
      totaluse++;
    }
  }
  *n_array_used += n_array_extra;
  return totaluse;
}


static void setarrayvector (lua_State *L, Table *t, int size) {
  int i;
  luaM_reallocvector(L, t->array, t->sizearray, size, TValue);
  for (i = t->sizearray; i < size; i++)
     setnilvalue(&t->array[i]);
  t->sizearray = size;
}


static void setnodevector (lua_State *L, Table *t, int size) {
  /* Exponent of the nearest upper power of 2 to `size`. */
  int lsize;

  /* No elements to hash part, also `ceillog2(0)` is invalid. */
  if (size == 0) {
    t->node = cast(Node *, dummynode);  /* use common `dummynode' */
    lsize = 0;
  }
  else {
    int i;
    lsize = ceillog2(size);
    if (lsize > MAXBITS) {
      luaG_runerror(L, "table overflow");
    }
    size = twoto(lsize);
    t->node = luaM_newvector(L, size, Node);
    /* Initialize new node array. */
    for (i = 0; i < size; i++) {
      Node *n = gnode(t, i);
      TValue *v = gval(n);

      gnext(n) = NULL;
      /* Can't store in `TValue *` because `TKey::nk` is an anonymous type. */
      setnilvalue(gkey(n));
      setnilvalue(v);
    }
  }
  t->lsizenode = cast_byte(lsize);
  t->lastfree = gnode(t, size);  /* all positions are free */
}


static void resize (lua_State *L, Table *t, int nasize, int nhsize) {
  int i;
  int oldasize = t->sizearray;
  int oldhsize = t->lsizenode;
  Node *nold = t->node;  /* save old hash ... */
  if (nasize > oldasize)  /* array part must grow? */
    setarrayvector(L, t, nasize);
  /* create new hash part with appropriate size */
  setnodevector(L, t, nhsize);
  if (nasize < oldasize) {  /* array part must shrink? */
    t->sizearray = nasize;
    /* re-insert elements from vanishing slice */
    for (i = nasize; i < oldasize; i++) {
      TValue *src = &t->array[i];
      if (!ttisnil(src)) {
        /* Move non-nil array index from array to hash. */
        TValue *dst = luaH_setnum(L, t, i + 1);
        setobjt2t(L, dst, src);
      }
    }
    /* shrink array */
    luaM_reallocvector(L, t->array, oldasize, nasize, TValue);
  }
  /* Copy elements from current hash part to newly allocated hash.
    This may also move elements from the hash part to the array part and
    vice versa. */
  for (i = twoto(oldhsize) - 1; i >= 0; i--) {
    Node *old = &nold[i];
    TValue *v = gval(old);
    if (!ttisnil(v)) {
      TValue *k = key2tval(old);
      TValue *dst = luaH_set(L, t, k);
      setobjt2t(L, dst, v);
    }
  }
  /* This table owned its node array? Can free it in that case. */
  if (nold != dummynode) {
    luaM_freearray(L, nold, twoto(oldhsize), Node);  /* free old array */
  }
}


void luaH_resizearray (lua_State *L, Table *t, int nasize) {
  int nsize = (t->node == dummynode) ? 0 : sizenode(t);
  resize(L, t, nasize, nsize);
}


static void rehash (lua_State *L, Table *t, const TValue *ek) {
  int nasize, na;

  /**
   * @brief
   *    Number of keys found between each power of 2 range: 2^(i-1) and 2^i.
   *    This is used to help find the optimal size given all possible
   *    array indices.
   *
   *    See Python output of the following:
   *
   *    `for i in range(1, 27):
   *      print(f"nums[{i-1}] = [{2**{i-1}}, {2**i})")`
   *
   * @details(2025-08-08) Index ranges for each power of 2 exponent:
   *
   *    [0] = [1, 2),   [1]  = [2, 4), [2]  = [4, 8)
   *    [3]  = [8, 16), [4]  = [16, 32), ...
   */
  int nums[MAXBITS + 1];
  int i;
  int totaluse;
  for (i = 0; i <= MAXBITS; i++) nums[i] = 0;  /* reset counts */

  nasize = numusearray(t, nums);  /* count keys in array part */
  totaluse = nasize;  /* all those keys are integer keys */
  totaluse += numusehash(t, nums, &nasize);  /* count keys in hash part */
  /* check if key is also an array index */
  nasize += countint(ek, nums);

  /* key is always added to total count regardless */
  totaluse++;

  /* compute new size for array part */
  na = computesizes(nums, &nasize);

  /* resize the table to new computed sizes */
  resize(L, t, nasize, totaluse - na);
}



/*
** }=============================================================
*/


Table *luaH_new (lua_State *L, int narray, int nhash) {
  Table *t = luaM_new(L, Table);
  luaC_link(L, obj2gco(t), LUA_TTABLE);
  t->metatable = NULL;
  t->flags = cast_byte(~0);
  /* temporary values (kept only if some malloc fails) */
  t->array = NULL;
  t->sizearray = 0;
  t->lsizenode = 0;
  t->node = cast(Node *, dummynode);
  setarrayvector(L, t, narray);
  setnodevector(L, t, nhash);
  return t;
}


void luaH_free (lua_State *L, Table *t) {
  if (t->node != dummynode)
    luaM_freearray(L, t->node, sizenode(t), Node);
  luaM_freearray(L, t->array, t->sizearray, TValue);
  luaM_free(L, t);
}


static Node *getfreepos (Table *t) {
  while (t->lastfree-- > t->node) {
    if (ttisnil(gkey(t->lastfree)))
      return t->lastfree;
  }
  return NULL;  /* could not find a free place */
}



/*
** inserts a new key into a hash table; first, check whether key's main
** position is free. If not, check whether colliding node is in its main
** position or not: if it is not, move colliding node to an empty place and
** put new key in its main position; otherwise (colliding node is in its main
** position), new key goes to an empty position.
*/
static TValue *newkey (lua_State *L, Table *t, const TValue *key) {
  Node *mp = mainposition(t, key);
  /* ideal position is occupied (collision) or we have no node array? */
  if (!ttisnil(gval(mp)) || mp == dummynode) {
    Node *othern;
    Node *n = getfreepos(t);  /* get a free place */
    if (n == NULL) {  /* cannot find a free place? */
      rehash(L, t, key);  /* grow table */
      return luaH_set(L, t, key);  /* re-insert key into grown table */
    }
    lua_assert(n != dummynode);
    othern = mainposition(t, key2tval(mp));
    if (othern != mp) {  /* is colliding node out of its main position? */
      /* yes; move colliding node into free position */
      while (gnext(othern) != mp) {
        othern = gnext(othern);
      }
      gnext(othern) = n;  /* redo the chain with `n' in place of `mp' */
      *n = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
      gnext(mp) = NULL;  /* now `mp' is free */
      setnilvalue(gval(mp));
    }
    else {  /* colliding node is in its own main position */
      /* new node will go into free position */
      gnext(n) = gnext(mp);  /* chain new position */
      gnext(mp) = n;
      mp = n;
    }
  }
  /* Set key for this node. */
  gkey(mp)->value = key->value;
  gkey(mp)->tt = key->tt;
  luaC_barriert(L, t, key);
  lua_assert(ttisnil(gval(mp)));
  return gval(mp);
}


/*
** search function for integers
*/
const TValue *luaH_getnum (Table *t, int key) {
  /* (1 <= key && key <= t->sizearray) */
  if (cast(unsigned int, key-1) < cast(unsigned int, t->sizearray))
    return &t->array[key-1];
  else {
    lua_Number nk = cast_num(key);
    Node *n = hashnum(t, nk);
    do {
      /* check whether `key' is somewhere in the chain */
      if (ttisnumber(gkey(n)) && luai_numeq(nvalue(gkey(n)), nk)) {
        return gval(n);  /* that's it */
      }
      else {
        n = gnext(n);
      }
    } while (n);
    return luaO_nilobject;
  }
}


/*
** search function for strings
*/
const TValue *luaH_getstr (Table *t, TString *key) {
  Node *n = hashstr(t, key);
  do {  /* check whether `key' is somewhere in the chain */
    if (ttisstring(gkey(n)) && rawtsvalue(gkey(n)) == key)
      return gval(n);  /* that's it */
    else n = gnext(n);
  } while (n);
  return luaO_nilobject;
}


/*
** main search function
*/
const TValue *luaH_get (Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNIL: return luaO_nilobject;
    case LUA_TSTRING: return luaH_getstr(t, rawtsvalue(key));
    case LUA_TNUMBER: {
      int k;
      lua_Number n = nvalue(key);
      lua_number2int(k, n);
      if (luai_numeq(cast_num(k), nvalue(key))) /* index is int? */
        return luaH_getnum(t, k);  /* use specialized version */
      /* else go through */
    }
    default: {
      Node *n = mainposition(t, key);
      do {  /* check whether `key' is somewhere in the chain */
        if (luaO_rawequalObj(key2tval(n), key))
          return gval(n);  /* that's it */
        else n = gnext(n);
      } while (n);
      return luaO_nilobject;
    }
  }
}


TValue *luaH_set (lua_State *L, Table *t, const TValue *key) {
  const TValue *p = luaH_get(t, key);
  t->flags = 0;
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else {
    if (ttisnil(key)) luaG_runerror(L, "table index is nil");
    else if (ttisnumber(key) && luai_numisnan(nvalue(key)))
      luaG_runerror(L, "table index is NaN");
    return newkey(L, t, key);
  }
}


TValue *luaH_setnum (lua_State *L, Table *t, int key) {
  const TValue *p = luaH_getnum(t, key);
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else {
    TValue k;
    setnvalue(&k, cast_num(key));
    return newkey(L, t, &k);
  }
}


TValue *luaH_setstr (lua_State *L, Table *t, TString *key) {
  const TValue *p = luaH_getstr(t, key);
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else {
    TValue k;
    setsvalue(L, &k, key);
    return newkey(L, t, &k);
  }
}


static int unbound_search (Table *t, unsigned int j) {
  unsigned int i = j;  /* i is zero or a present index */
  j++;
  /* find `i' and `j' such that i is present and j is not */
  while (!ttisnil(luaH_getnum(t, j))) {
    i = j;
    j *= 2;
    if (j > cast(unsigned int, MAX_INT)) {  /* overflow? */
      /* table was built with bad purposes: resort to linear search */
      i = 1;
      while (!ttisnil(luaH_getnum(t, i))) i++;
      return i - 1;
    }
  }
  /* now do a binary search between them */
  while (j - i > 1) {
    unsigned int m = (i+j)/2;
    if (ttisnil(luaH_getnum(t, m))) j = m;
    else i = m;
  }
  return i;
}


/*
** Try to find a boundary in table `t'. A `boundary' is an integer index
** such that t[i] is non-nil and t[i+1] is nil (and 0 if t[1] is nil).
*/
int luaH_getn (Table *t) {
  unsigned int j = t->sizearray;
  if (j > 0 && ttisnil(&t->array[j - 1])) {
    /* there is a boundary in the array part: (binary) search for it */
    unsigned int i = 0;
    while (j - i > 1) {
      unsigned int m = (i+j)/2;
      if (ttisnil(&t->array[m - 1])) j = m;
      else i = m;
    }
    return i;
  }
  /* else must find a boundary in hash part */
  else if (t->node == dummynode)  /* hash part is empty? */
    return j;  /* that is easy... */
  else return unbound_search(t, j);
}



#if defined(LUA_DEBUG)

Node *luaH_mainposition (const Table *t, const TValue *key) {
  return mainposition(t, key);
}

int luaH_isdummy (Node *n) { return n == dummynode; }

#endif
