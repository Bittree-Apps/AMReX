#include <AMReX_BittreeFunct.H>

using namespace amrex;

void
amrex::BittreeFunct::operator() (int lbase, Real time, int& new_finest, BoxArray& new_grids, DistributionMapping& bittree_dmap)
{
    if (bittree_func != nullptr) {
        bittree_func (lbase,time,new_finest,new_grids,bittree_dmap);
    } else {
        amrex::Abort("BittreeFunct::bittree_func is null");
    }
}
