module amrex_bittree_module

  use iso_c_binding
  use amrex_fort_module

  implicit none

  private

  public :: amrex_bittree_procedure

  interface
     subroutine amrex_bittree_procedure (bittree_flg) bind(c)
       import
       integer(c_int), value :: bittree_flg
     end subroutine amrex_bittree_procedure
  end interface

end module amrex_bittree_module
