#include <AMReX_FNewGrid.H>

using namespace amrex;

extern "C"
{
    void amrex_fi_new_newgrid (FNewGrid*& fng, FNewGrid::newgrid_funptr_t fnewgrid)
    {
        fng = new FNewGrid(fnewgrid);
    }

    void amrex_fi_delete_newgrid (FNewGrid* fng)
    {
        delete fng;
    }
}
