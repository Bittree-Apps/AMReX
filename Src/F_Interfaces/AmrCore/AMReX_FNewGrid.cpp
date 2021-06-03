#include <AMReX_FNewGrid.H>

using namespace amrex;

void
amrex::FNewGrid::operator() (int callbackflg)
{
    if (newgrid != nullptr) {
        newgrid(callbackflg);
    } else {
        amrex::Abort("FNewGrid::newgrid is null");
    }
}
