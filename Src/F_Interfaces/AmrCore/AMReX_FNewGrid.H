#ifndef AMREX_FNEWGRID_H_
#define AMREX_FNEWGRID_H_

#include <AMReX_Config.H>
#include <AMReX_BCRec.H>
#include <AMReX_Geometry.H>
#include <AMReX_MultiFab.H>
#include <AMReX_ArrayLim.H>
#include <AMReX_FilCC_C.H>
#include <AMReX_FilND_C.H>
#include <AMReX_TypeTraits.H>

namespace amrex
{

class FNewGrid
{
public:
    using newgrid_funptr_t = void(*)(int callbackflg);

    FNewGrid (newgrid_funptr_t fnewgrid) : newgrid(fnewgrid) {}

    ~FNewGrid () {}
    void operator() (int callbackflg);

private:
    newgrid_funptr_t newgrid = nullptr;
};

}
#endif
