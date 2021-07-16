#include <AMReX_BittreeFunct.H>

using namespace amrex;

void
amrex::BittreeFunct::operator() (int lev, Real time, int& new_finest, BoxArray& bittree_grid, DistributionMapping& bittree_dmap)
{
    if (bittree_func != nullptr) {
        bittree_func (lev,time,new_finest,bittree_grid,bittree_dmap);
    } else {
        amrex::Abort("BittreeFunct::bittree_func is null");
    }
}
