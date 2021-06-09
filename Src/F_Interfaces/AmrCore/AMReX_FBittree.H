#ifndef AMREX_FBittree_H_
#define AMREX_FBittree_H_

#include <AMReX_BittreeFunct.H>

namespace amrex
{
class FBittree : public BittreeFunct
{
public:
    FBittree (bittree_funptr_t bittree_callback) : BittreeFunct(bittree_callback) {}

    ~FBittree () {}

};
}
#endif
