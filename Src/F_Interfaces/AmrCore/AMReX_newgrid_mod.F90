module amrex_newgrid_module

  use iso_c_binding
  use amrex_fort_module

  implicit none

  private

  public :: amrex_newgrid_procedure

  interface
     subroutine amrex_newgrid_procedure (callbackflg) bind(c)
       import
       integer(c_int), value :: callbackflg
     end subroutine amrex_newgrid_procedure
  end interface

end module amrex_newgrid_module
