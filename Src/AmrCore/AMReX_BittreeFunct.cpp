#include <AMReX_BittreeFunct.H>

using namespace amrex;

void
amrex::BittreeFunct::operator() (int bittree_flg)
{
    if (bittree_func != nullptr) {
        bittree_func (bittree_flg);
    } else {
        amrex::Abort("BittreeFunct::bittree_func is null");
    }
}
