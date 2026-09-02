/* This includes all primes up to 46337 (sufficient for 2 billion ranks).
   If storage is a concern (MPICH was originally organized so that only
   code and data needed by applications would be loaded in order to keep
   both the size of the executable and any shared library loads small),
   this include could be organized include only primes up to 1000 (for
   a maximum of a million ranks) or 3400 (for a maximum of 10 million ranks),
   which will significantly reduce the number of elements included here.
*/
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "hxmesh_dims.h"
#include "hxmesh_primes.h"

/* Because we store factors with their multiplicities, a small array can
   store all of the factors for a large number (grows *faster* than n
   factorial). */
#define MAX_FACTORS 10
/* 2^20 is a million */
#define MAX_DIMS    20

typedef struct Factors {
    int val, cnt;
} Factors;

/* Local only routines.  These should *not* have standard prefix */
static int factor_num(int, Factors[], int *);
static int ndivisors_from_factor(int nf, const Factors * factors);
static int factor_to_divisors(int nf, Factors * factors, int ndivs, int divs[]);
static void factor_to_dims_by_rr(int nf, Factors f[], int nd, int dims[]);
static int optbalance(int n, int idx, int nd, int ndivs,
                      const int divs[], int trydims[], int *curbal_p, int optdims[]);

/*
 * Factor "n", returning the prime factors and their counts in factors, in
 * increasing order.  Return the sum of the number of primes, including their
 * powers (e.g., 2^3 * 7^5 * 19 gives 9 divisors, the maximum that can be created
 * from this factorization)
 */
static int factor_num(int nn, Factors factors[], int *nprimes)
{
    int n = nn, val;
    int i, nfactors = 0, nall = 0;
    int cnt;

    /* Find the prime number that less than that value and try dividing
     * out the primes.  */

    /* First, get factors of 2 without division */
    if ((n & 0x1) == 0) {
        cnt = 1;
        n >>= 1;
        while ((n & 0x1) == 0) {
            cnt++;
            n >>= 1;
        }
        factors[0].cnt = cnt;
        factors[0].val = 2;
        nfactors = 1;
        nall = cnt;
    }

    /* An earlier version computed an approximation to the sqrt(n) to serve
     * as a stopping criteria.  However, tests showed that checking that the
     * square of the current prime is less than the remaining value (after
     * dividing out the primes found so far) is faster.
     */
    i = 1;
    do {
        int n2;
        val = primes[i];
        /* Note that we keep removing factors from n, so this test becomes
         * easier and easier to satisfy as we remove factors from n
         */
        if (val * val > n)
            break;
        n2 = n / val;
        if (n2 * val == n) {
            cnt = 1;
            n = n2;
            n2 = n / val;
            while (n2 * val == n) {
                cnt++;
                n = n2;
                n2 = n / val;
            }
            /* Test on nfactors >= MAX_FACTORS?  */
            /* --BEGIN ERROR HANDLING-- */
            if (nfactors + 1 == MAX_FACTORS) {
                /* Time to panic.  This should not happen, since the
                 * smallest number that could exceed this would
                 * be the product of the first 10 primes that are
                 * greater than one, which is 6,469,693,230 */
                return nfactors;
            }
            /* --END ERROR HANDLING-- */
            factors[nfactors].val = val;
            factors[nfactors++].cnt = cnt;
            nall += cnt;
            if (n == 1)
                break;
        }
        i++;
    } while (1);
    if (n != 1) {
        /* There was one factor > sqrt(n).  This factor must be prime.
         * Note if nfactors was 0, n was prime */
        factors[nfactors].val = n;
        factors[nfactors++].cnt = 1;
        nall++;
    }
    *nprimes = nall;
    return nfactors;
}

static int ndivisors_from_factor(int nf, const Factors * factors)
{
    int i, ndiv;
    ndiv = 1;
    for (i = 0; i < nf; i++) {
        ndiv *= (factors[i].cnt + 1);
    }
    ndiv -= 2;

    return ndiv;
}

static int factor_to_divisors(int nf, Factors * factors, int ndiv, int divs[])
{
    int i, powers[MAX_FACTORS], curbase[MAX_FACTORS], nd, idx, val, mpi_errno;

    for (i = 0; i < nf; i++) {
        powers[i] = 0;
        curbase[i] = 1;
    }

    for (nd = 0; nd < ndiv; nd++) {
        /* Get the next power */
        for (idx = 0; idx < nf; idx++) {
            powers[idx]++;
            if (powers[idx] > factors[idx].cnt) {
                powers[idx] = 0;
                curbase[idx] = 1;
            } else {
                curbase[idx] *= factors[idx].val;
                break;
            }
        }
        /* Compute the divisor.  Note that we could keep the scan of values
         * from k to nf-1, then curscan[0] would always be the next value */
        val = 1;
        for (idx = 0; idx < nf; idx++)
            val *= curbase[idx];
        divs[nd] = val;
    }

    /* Values are not sorted - for example, 2(2),3 will give 2,4,3 as the
     * divisors */
    /* This code is used because it is significantly faster than using
     * the qsort routine.  In tests of several million dims_create
     * calls, this code took 1.02 seconds (with -O3) and 1.79 seconds
     * (without optimization) while qsort took 2.5 seconds.
     */
    if (nf > 1) {
        int gap, j, j1, j2, k, j1max, j2max;
        int *divs2;
        int *restrict d1, *restrict d2;

        divs2 = (int *) malloc(nd * sizeof(int));
        assert(divs2!=NULL);

        /* handling the first set of pairs separately saved about 20%;
         * done inplace */
        for (j = 0; j < nd - 1; j += 2) {
            if (divs[j] > divs[j + 1]) {
                int tmp = divs[j];
                divs[j] = divs[j + 1];
                divs[j + 1] = tmp;
            }
        }
        /* Using pointers d1 and d2 about 2x copying divs2 back into divs
         * at the end of each phase */
        d1 = divs;
        d2 = divs2;
        for (gap = 2; gap < nd; gap *= 2) {
            k = 0;
            for (j = 0; j + gap < nd; j += gap * 2) {
                j1 = j;
                j2 = j + gap;
                j1max = j2;
                j2max = j2 + gap;
                if (j2max > nd)
                    j2max = nd;
                while (j1 < j1max && j2 < j2max) {
                    if (d1[j1] < d1[j2]) {
                        d2[k++] = d1[j1++];
                    } else {
                        d2[k++] = d1[j2++];
                    }
                }
                /* Copy remainder */
                while (j1 < j1max)
                    d2[k++] = d1[j1++];
                while (j2 < j2max)
                    d2[k++] = d1[j2++];
            }
            /* May be some left over */
            while (j < nd)
                d2[k++] = d1[j++];
            /* swap pointers and start again */
            {
                int *dt = d1;
                d1 = d2;
                d2 = dt;
            }
        }

        /* Result must end up in divs */
        if (d1 != divs) {
            for (j1 = 0; j1 < nd; j1++)
                divs[j1] = d1[j1];
        }
        free(divs2);
    }
    return nd;
}


/*
 * This is a modified round robin assignment.  The goal is to
 * get a good first guess at a good distribution.
 *
 * First, distribute factors to dims[0..nd-1].  The purpose is to get the
 * initial factors set and to ensure that the smallest dimension is > 1.
 * Second, distribute the remaining factors, starting with the largest, to
 * the elements of dims with the smallest index i such that
 *   dims[i-1] > dims[i]*val
 * or to dims[0] if no i satisfies.
 * For example, this will successfully distribute the factors of 100 in 3-d
 * as 5,5,4.  A pure round robin would give 10,5,2.
 */
static void factor_to_dims_by_rr(int nf, Factors f[], int nd, int dims[])
{
    int i, j, k, cnt, val;

    /* Initialize dims */
    for (i = 0; i < nd; i++)
        dims[i] = 1;

    k = 0;
    /* Start with the largest factors, move back to the smallest factor */
    for (i = nf - 1; i >= 0; i--) {
        val = f[i].val;
        cnt = f[i].cnt;
        for (j = 0; j < cnt; j++) {
            if (k < nd) {
                dims[k++] = val;
            } else {
                int kk;
                /* find first location where apply val is valid.
                 * Can always apply at dims[0] */
                for (kk = nd - 1; kk > 0; kk--) {
                    if (dims[kk] * val <= dims[kk - 1])
                        break;
                }
                dims[kk] *= val;
            }
        }
    }
}


/* need to set a minidx where it stops because the remaining
   values are known.  Then pass in the entire array.  This is needed
   to get the correct values for "ties" between the first and last values.
 */
static int optbalance(int n, int idx, int nd, int ndivs, const int divs[],
                      int trydims[], int *curbal_p, int optdims[])
{
    int min = trydims[nd - 1], curbal = *curbal_p, testbal;
    int k, f, q, ff, i, ii, kk, nndivs, sf, mpi_errno = 0;

    if (idx > 1) {
        int *newdivs;
        newdivs = (int *) malloc(ndivs * sizeof(int));
        assert(newdivs!=NULL);

        /* At least 3 divisors to set (0...idx).  We try all choices
         * recursively, but stop looking when we can easily tell that
         * no additional cases can improve the current solution. */
        for (k = 0; k < ndivs; k++) {
            f = divs[k];
            if (idx < nd - 1 && f - min > curbal) {
                break;  /* best case is all divisors at least
                         * f, so the best balance is at least
                         * f - min */
            }
            q = n / f;
            /* check whether f is a divisor of what's left.  If so,
             * remember it as the smallest new divisor */
            /* sf is the smallest remaining factor; we use this in an
             * optimization for possible divisors */
            nndivs = 0;
            if (q % f == 0) {
                newdivs[nndivs++] = f;
                sf = f;
            } else if (k + 1 < ndivs) {
                sf = divs[k + 1];
            } else {
                /* run out of next factors, bail out */
                break;
            }
            if (idx < nd - 1 && sf - min > curbal) {
                break;
            }
            ff = sf * sf;       /* At least 2 more divisors */
            for (ii = idx - 1; ii > 0 && ff <= q; ii--)
                ff *= sf;
            if (ii > 0) {
                break;  /* remaining divisors >= sf, and
                         * product must be <= q */
            }

            /* Try f as the divisor at the idx location */
            trydims[idx] = f;
            /* find the subset of divisors of n that are divisors of q
             * and larger than f but such that f*f <= q */
            for (kk = k + 1; kk < ndivs; kk++) {
                f = divs[kk];
                ff = f * f;
                if (ff > q) {
                    break;
                }
                if ((q % f) == 0) {
                    newdivs[nndivs++] = f;
                }
                /* sf * f <= q, break otherwise */
            }
            /* recursively try to find the best subset */
            if (nndivs > 0) {
                optbalance(q, idx - 1, nd, nndivs, newdivs, trydims, curbal_p, optdims);
            }
        }
        free(newdivs);
    } else if (idx == 1) {
        /* Only two.  Find the pair such that q,f has min value for q-min, i.e.,
         * the smallest q such that q > f.  Note that f*f < n if q > f and
         * q*f = n */
        /* Valid choices for f, q >= divs[0] */
        int qprev = -1;
        for (k = 1; k < ndivs; k++) {
            f = divs[k];
            q = n / f;
            if (q < f)
                break;
            qprev = q;
        }
        f = divs[k - 1];
        if (qprev > 0)
            q = qprev;
        else
            q = n / f;

        if (q < f) {
            /* No valid solution.  Exit without changing current optdims */
            goto fn_exit;
        }
        /* If nd is 2 (and not greater), we will be replacing all values
         * in dims.  In that case, testbal is q-f;
         */
        if (nd == 2)
            testbal = q - f;
        else
            testbal = q - min;

        /* Take the new value if it is at least as good as the
         * current choice.  This is what Jesper Traeff's version does
         * (see the code he provided for his EuroMPI'15 paper) */
        if (testbal <= curbal) {
            for (i = 2; i < nd; i++)
                optdims[i] = trydims[i];
            optdims[0] = q;
            optdims[1] = f;
            /* Use the balance for the range of dims, not the total
             * balance */
            *curbal_p = q - min;
        }
    } else {
        /* idx == 0.  Only one choice for divisor */
        if (n - min <= curbal) {
            for (i = 1; i < nd; i++)
                optdims[i] = trydims[i];
            optdims[0] = n;
            *curbal_p = n - min;
        }
    }

  fn_exit:
    return mpi_errno;
}


/* FIXME: The error checking should really be part of MPI_Dims_create,
   not part of MPIR_Dims_create_impl.  That slightly changes the
   semantics of Dims_create provided by the device, but only by
   removing the need to check for errors */

int mpich_dims_create(int nnodes, int ndims, int dims[])
{
    Factors f[MAX_FACTORS];
    int nf, nprimes = 0, i, j, k, val, nextidx;
    int ndivs, curbal;
    int trydims[MAX_DIMS];
    int dims_needed, dims_product, mpi_errno = 0;
    int chosen[MAX_DIMS], foundDecomp;
    int *divs;

    /* Find the number of unspecified dimensions in dims and the product
     * of the positive values in dims */
    dims_needed = 0;
    dims_product = 1;
    for (i = 0; i < ndims; i++) {
        assert(dims[i]>=0);
        if (dims[i] == 0)
            dims_needed++;
        else
            dims_product *= dims[i];
    }

    /* Can we factor nnodes by dims_product? */
    assert((nnodes / dims_product) * dims_product == nnodes); 

    if (!dims_needed) {
        /* Special case - all dimensions provided */
        return 0;
    }

    assert(dims_needed <= MAX_DIMS);

    nnodes /= dims_product;

    /* Factor the remaining nodes into dims_needed components.  The
     * MPI standard says that these should be as balanced as possible;
     * in particular, unfortunately, not necessarily arranged in a way
     * that makes sense for the underlying machine topology.  The approach
     * used here was inspired by "Specification Guideline Violations
     * by MPI_Dims_create," by Jesper Traeff and Felix Luebbe, EuroMPI'15.
     * Among the changes are the use of a table of primes to speed up the
     * process of finding factors, and some effort to reduce the number
     * of integer division operations.
     *
     * Values are put into chosen[0..dims_needed].  These values are then
     * copied back into dims into the "empty" slots (values == 0).
     */

    if (dims_needed == 1) {
        /* Simply put the correct value in place */
        for (j = 0; j < ndims; j++) {
            if (dims[j] == 0) {
                dims[j] = nnodes;
                break;
            }
        }
        return 0;
    }

    /* Factor nnodes */
    nf = factor_num(nnodes, f, &nprimes);

    /* Remove primes > sqrt(n) */
    nextidx = 0;
    for (j = nf - 1; j > 0 && nextidx < dims_needed - 1; j--) {
        val = f[j].val;
        if (f[j].cnt == 1 && val * val > nnodes) {
            chosen[nextidx++] = val;
            nf--;
            nnodes /= val;
            nprimes--;
        } else
            break;
    }

    /* Now, handle some special cases.  If we find *any* of these,
     * we're done and can use the values in chosen to return values in dims */
    foundDecomp = 0;
    if (nprimes + nextidx <= dims_needed) {
        /* Fill with the primes */
        int m, cnt;
        i = nextidx + nprimes;
        for (k = 0; k < nf; k++) {
            cnt = f[k].cnt;
            val = f[k].val;
            for (m = 0; m < cnt; m++)
                chosen[--i] = val;
        }
        i = nextidx + nprimes;
        while (i < ndims)
            chosen[i++] = 1;
        foundDecomp = 1;
    } else if (dims_needed - nextidx == 1) {
        chosen[nextidx] = nnodes;
        foundDecomp = 1;
    } else if (nf == 1) {
        /* What's left is a product of a single prime, so there is only one
         * possible factorization */
        int cnt = f[0].cnt;
        val = f[0].val;
        for (k = nextidx; k < dims_needed; k++)
            chosen[k] = 1;
        k = nextidx;
        while (cnt-- > 0) {
            if (k >= dims_needed)
                k = nextidx;
            chosen[k++] *= val;
        }
        foundDecomp = 1;
    }
    /* There's another case we consider, particularly since we've
     * factored out the "large" primes.  If (cnt % (ndims-nextidx)) == 0
     * for every remaining factor, then
     * f = product of (val^ (cnt/(ndims-nextidx)))
     */
    if (!foundDecomp) {
        int ndleft = dims_needed - nextidx;
        for (j = 0; j < nf; j++)
            if (f[j].cnt % ndleft != 0)
                break;
        if (j == nf) {
            int fval;
            val = 1;
            for (j = 0; j < nf; j++) {
                int pow = f[j].cnt / ndleft;
                fval = f[j].val;
                while (pow--)
                    val *= fval;
            }
            for (j = nextidx; j < dims_needed; j++)
                chosen[j] = val;
            foundDecomp = 1;
        }
    }

    if (foundDecomp) {
        j = 0;
        for (i = 0; i < ndims; i++) {
            if (dims[i] == 0) {
                dims[i] = chosen[j++];
            }
        }
        return 0;
    }

    /* ndims >= 2 */

    /* No trivial solution.  Balance the remaining values (note that we may
     * have already trimmed off some large factors */
    /* First, find all of the divisors given by the remaining factors */
    ndivs = ndivisors_from_factor(nf, (const Factors *) f);
    divs = (int *) malloc(((unsigned int) ndivs) * sizeof(int));
    assert(divs!=NULL);

    ndivs = factor_to_divisors(nf, f, ndivs, divs);

    /* Create a simple first guess.  We do this by using a round robin
     * distribution of the primes in the remaining values (from nextidx
     * to ndims) */
    factor_to_dims_by_rr(nf, f, dims_needed - nextidx, chosen + nextidx);

    /* This isn't quite right, as the balance may depend on the other (larger)
     * divisors already chosen */
    /* Need to add nextidx */
    curbal = chosen[0] - chosen[dims_needed - 1];
    trydims[dims_needed - nextidx - 1] = divs[0];

    mpi_errno = optbalance(nnodes, dims_needed - nextidx - 1, dims_needed - nextidx,
                           ndivs, divs, trydims, &curbal, chosen + nextidx);
    free(divs);

    j = 0;
    for (i = 0; i < ndims; i++) {
        if (dims[i] == 0) {
            dims[i] = chosen[j++];
        }
    }

  fn_exit:
    return mpi_errno;
}
/*
int main(int argc, char**argv)
{
    int nnodes = 1024;
    int dims[2] = {0, 0};

    mpich_dims_create(nnodes, 2, dims);
    printf("Nodes: %d; dims[0]: %d; dims[1]: %d\n", nnodes, dims[0], dims[1]);
    return 0;
}
*/