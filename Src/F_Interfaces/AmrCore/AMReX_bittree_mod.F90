module amrex_bittree_module

  use iso_c_binding
  use amrex_fort_module

  implicit none

  private

  public :: amrex_bittree_procedure

  interface
     subroutine amrex_bittree_procedure (lbase,time,new_finest,new_grid,bittree_dmap) bind(c)
       import
       integer(c_int), value :: lbase
       real(amrex_real), value :: time
       type(c_ptr), value :: new_finest,new_grid,bittree_dmap
     end subroutine amrex_bittree_procedure
  end interface

end module amrex_bittree_module
