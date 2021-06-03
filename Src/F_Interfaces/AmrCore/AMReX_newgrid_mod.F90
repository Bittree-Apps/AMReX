module amrex_newgrid_module

  use iso_c_binding
  use amrex_fort_module

  implicit none

  private

  public :: amrex_newgrid_build, amrex_newgrid_destroy, amrex_newgrid_proc

  type, public :: amrex_newgrid
     logical     :: owner = .false.
     type(c_ptr) :: p     = c_null_ptr
   contains
     generic :: assignment(=) => amrex_newgrid_assign  ! shallow copy
     procedure, private :: amrex_newgrid_assign
#if !defined(__GFORTRAN__) || (__GNUC__ > 4)
     final :: amrex_newgrid_destroy
#endif
  end type amrex_newgrid

  interface
     subroutine amrex_newgrid_proc (callbackflg) bind(c)
       import
       logical(c_int), value :: callbackflg
     end subroutine amrex_newgrid_proc
  end interface

  interface
     subroutine amrex_fi_new_newgrid (fng, fnewgrid) bind(c)
       import
       implicit none
       type(c_ptr) :: fng
       type(c_funptr), value :: fnewgrid
     end subroutine amrex_fi_new_newgrid

     subroutine amrex_fi_delete_newgrid (fng) bind(c)
       import
       implicit none
       type(c_ptr), value :: fng
     end subroutine amrex_fi_delete_newgrid
  end interface

contains

  subroutine amrex_newgrid_build (fng, fnewgrid)
    type(amrex_newgrid), intent(inout) :: fng
    procedure(amrex_newgrid_proc) :: fnewgrid
    fng%owner = .true.
    call amrex_fi_new_newgrid(fng%p, c_funloc(fnewgrid))
  end subroutine amrex_newgrid_build

  impure elemental subroutine amrex_newgrid_destroy (this)
    type(amrex_newgrid), intent(inout) :: this
    if (this%owner) then
       if (c_associated(this%p)) then
          call amrex_fi_delete_newgrid(this%p)
       end if
    end if
    this%owner = .false.
    this%p     = c_null_ptr
  end subroutine amrex_newgrid_destroy

  subroutine amrex_newgrid_assign (dst, src)
    class(amrex_newgrid), intent(inout) :: dst
    type (amrex_newgrid), intent(in   ) :: src
    call amrex_newgrid_destroy(dst)
    dst%owner = .false.
    dst%p     = src%p
  end subroutine amrex_newgrid_assign

end module amrex_newgrid_module

