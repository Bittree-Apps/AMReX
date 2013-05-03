#include <winstd.H>

#include <algorithm>
#include <iomanip>

#include <ParmParse.H>
#include <ParallelDescriptor.H>
#include <Utility.H>
#include <LO_BCTYPES.H>
#include <CG_F.H>
#include <CGSolver.H>
#include <MultiGrid.H>

namespace
{
    bool initialized = false;
}
//
// Set default values for these in Initialize()!!!
//
int              CGSolver::def_maxiter;
int              CGSolver::def_verbose;
CGSolver::Solver CGSolver::def_cg_solver;
bool             CGSolver::use_jbb_precond;
bool             CGSolver::use_jacobi_precond;
double           CGSolver::def_unstable_criterion;

void
CGSolver::Initialize ()
{
    if (initialized) return;
    //
    // Set defaults here!!!
    //
    CGSolver::def_maxiter            = 40;
    CGSolver::def_verbose            = 0;
    CGSolver::def_cg_solver          = BiCGStab;
    CGSolver::use_jbb_precond        = 0;
    CGSolver::use_jacobi_precond     = 0;
    CGSolver::def_unstable_criterion = 10;

    ParmParse pp("cg");

    pp.query("v",                  def_verbose);
    pp.query("maxiter",            def_maxiter);
    pp.query("verbose",            def_verbose);
    pp.query("use_jbb_precond",    use_jbb_precond);
    pp.query("use_jacobi_precond", use_jacobi_precond);
    pp.query("unstable_criterion", def_unstable_criterion);

    int ii;
    if (pp.query("cg_solver", ii))
    {
        switch (ii)
        {
        case 0: def_cg_solver = CG;         break;
        case 1: def_cg_solver = BiCGStab;   break;
        case 2: def_cg_solver = CABiCGStab; break;
        default:
            BoxLib::Error("CGSolver::Initialize(): bad cg_solver");
        }
    }

    if (ParallelDescriptor::IOProcessor() && (def_verbose > 2))
    {
        std::cout << "CGSolver settings ...\n";
	std::cout << "   def_maxiter            = " << def_maxiter            << '\n';
	std::cout << "   def_unstable_criterion = " << def_unstable_criterion << '\n';
	std::cout << "   def_cg_solver          = " << def_cg_solver          << '\n';
	std::cout << "   use_jbb_precond        = " << use_jbb_precond        << '\n';
	std::cout << "   use_jacobi_precond     = " << use_jacobi_precond     << '\n';
    }

    BoxLib::ExecOnFinalize(CGSolver::Finalize);
    
    initialized = true;
}

void
CGSolver::Finalize ()
{
    initialized = false;
}

CGSolver::CGSolver (LinOp& _Lp,
                    bool   _use_mg_precond,
                    int    _lev)
    :
    Lp(_Lp),
    mg_precond(0),
    lev(_lev),
    use_mg_precond(_use_mg_precond)
{
    Initialize();
    maxiter = def_maxiter;
    verbose = def_verbose;
    set_mg_precond();
}

void
CGSolver::set_mg_precond ()
{
    delete mg_precond;
    if (use_mg_precond)
    {
        mg_precond = new MultiGrid(Lp);
    }
}

CGSolver::~CGSolver ()
{
    delete mg_precond;
}

static
void
Spacer (std::ostream& os, int lev)
{
    for (int k = 0; k < lev; k++)
    {
        os << "   ";
    }
}

static
Real
norm_inf (const MultiFab& res, bool local = false)
{
    Real restot = 0;

    for (MFIter mfi(res); mfi.isValid(); ++mfi) 
    {
        restot = std::max(restot, res[mfi].norm(mfi.validbox(), 0));
    }

    if ( !local )
        ParallelDescriptor::ReduceRealMax(restot);

    return restot;
}

int
CGSolver::solve (MultiFab&       sol,
                 const MultiFab& rhs,
                 Real            eps_rel,
                 Real            eps_abs,
                 LinOp::BC_Mode  bc_mode)
{
    switch (def_cg_solver)
    {
    case 0:
        return solve_cg(sol, rhs, eps_rel, eps_abs, bc_mode);
    case 1:
        return solve_bicgstab(sol, rhs, eps_rel, eps_abs, bc_mode);
    case 2:
        return solve_cabicgstab(sol, rhs, eps_rel, eps_abs, bc_mode);
    default:
        BoxLib::Error("CGSolver::solve(): unknown solver");
    }

    return -1;
}

static
void
sxay (MultiFab&       ss,
      const MultiFab& xx,
      Real            a,
      const MultiFab& yy,
      int             yycomp)
{
    BL_ASSERT(yy.nComp() > yycomp);

    const int ncomp  = 1;
    const int sscomp = 0;
    const int xxcomp = 0;

    for (MFIter mfi(ss); mfi.isValid(); ++mfi)
    {
        const Box&       ssbx  = mfi.validbox();
        FArrayBox&       ssfab = ss[mfi];
        const FArrayBox& xxfab = xx[mfi];
        const FArrayBox& yyfab = yy[mfi];

        FORT_CGSXAY(ssfab.dataPtr(sscomp),
                    ARLIM(ssfab.loVect()), ARLIM(ssfab.hiVect()),
                    xxfab.dataPtr(xxcomp),
                    ARLIM(xxfab.loVect()), ARLIM(xxfab.hiVect()),
                    &a,
                    yyfab.dataPtr(yycomp),
                    ARLIM(yyfab.loVect()), ARLIM(yyfab.hiVect()),
                    ssbx.loVect(), ssbx.hiVect(),
                    &ncomp);
    }
}

inline
void
sxay (MultiFab&       ss,
      const MultiFab& xx,
      Real            a,
      const MultiFab& yy)
{
    sxay(ss,xx,a,yy,0);
}

//
// Do a one-component dot product of r & z using supplied components.
//
static
Real
dotxy (const MultiFab& r,
       int             rcomp,
       const MultiFab& z,
       int             zcomp,
       bool            local)
{
    BL_ASSERT(r.nComp() > rcomp);
    BL_ASSERT(z.nComp() > zcomp);
    BL_ASSERT(r.boxArray() == z.boxArray());

    const int ncomp = 1;

    Real dot = 0;

    for (MFIter mfi(r); mfi.isValid(); ++mfi)
    {
        const Box&       rbx  = mfi.validbox();
        const FArrayBox& rfab = r[mfi];
        const FArrayBox& zfab = z[mfi];

        Real tdot;

        FORT_CGXDOTY(&tdot,
                     zfab.dataPtr(zcomp),
                     ARLIM(zfab.loVect()),ARLIM(zfab.hiVect()),
                     rfab.dataPtr(rcomp),
                     ARLIM(rfab.loVect()),ARLIM(rfab.hiVect()),
                     rbx.loVect(),rbx.hiVect(),
                     &ncomp);
        dot += tdot;
    }

    if ( !local )
        ParallelDescriptor::ReduceRealSum(dot);

    return dot;
}

inline
Real
dotxy (const MultiFab& r,
       const MultiFab& z,
       bool            local = false)
{
    return dotxy(r,0,z,0,local);
}

//
// The "S" in the Communication-avoiding BiCGStab algorithm.
//
const int SSS = 4;

//
// z[m] = alpha*A[m][n]*x[n]+beta*y[m]   [row][col]
//
inline
void
__gemv (Real* z,
        Real  alpha,
        Real  A[((4*SSS)+1)][((4*SSS)+1)],
        Real* x,
        Real  beta,
        Real* y,
        int   rows,
        int   cols)
{
    for (int r = 0;r < rows; r++)
    {
        Real sum = 0;
        for (int c = 0;c < cols; c++)
        {
            sum += A[r][c]*x[c];
        }
        z[r] = alpha*sum + beta*y[r];
    }
}

//
// z[n] = alpha*x[n]+beta*y[n]
//
inline
void
__axpy (Real* z,
        Real  alpha,
        Real* x,
        Real  beta,
        Real* y,
        int   n)
{
    for (int nn = 0; nn < n; nn++)
    {
        z[nn] = alpha*x[nn] + beta*y[nn];
    }
}

//
// x[n].y[n]
//
inline
Real
__dot (Real* x, Real* y, int n)
{
    Real sum = 0;
    for (int nn = 0; nn < n; nn++)
    {
        sum += x[nn]*y[nn];
    }
    return sum;
}

//
// z[n] = 0
//
inline
void
__zero (Real* z, int n)
{
    for (int nn = 0; nn < n;nn++)
    {
        z[nn] = 0;
    }
}

static
void
BuildGramMatrix (Real* Gg, const MultiFab& PR, const MultiFab& rt)
{
    BL_ASSERT(rt.nComp() == 1);
    BL_ASSERT(PR.nComp() == 4*SSS+1);

    const int Nrows = PR.nComp();
    const int Ncols = Nrows + 1;
    //
    // Gg is dimensioned (Ncols*Nrows).
    //
    // We take advantage of the symmetry in Gg.
    //
    for (int mm = 0; mm < Nrows; mm++)
    {
        for (int nn = mm; nn < Nrows; nn++)
        {
            Gg[mm*Ncols + nn] = dotxy(PR, mm, PR, nn, true);
        }

        for (int nn = 0; nn < mm; nn++)
        {
            Gg[mm*Ncols + nn] = Gg[nn*Ncols + mm];
        }

        Gg[mm*Ncols + Nrows] = dotxy(PR, mm, rt, 0, true);
    }

    ParallelDescriptor::ReduceRealSum(Gg, Nrows*Ncols);
}

//
// Based on Erin Carson/Jim Demmel/Nick Knight's s-Step BiCGStab Algorithm 3.4.
//
// As originally implemented by:
//
// Samuel Williams
// SWWilliams@lbl.gov
// Lawrence Berkeley National Lab
//

int
CGSolver::solve_cabicgstab (MultiFab&       sol,
                            const MultiFab& rhs,
                            Real            eps_rel,
                            Real            eps_abs,
                            LinOp::BC_Mode  bc_mode)
{
    BL_PROFILE("CGSolver::solve_cabicgstab()");

    BL_ASSERT(sol.nComp() == 1);
    BL_ASSERT(sol.boxArray() == Lp.boxArray(lev));
    BL_ASSERT(rhs.boxArray() == Lp.boxArray(lev));

    Real  temp1[4*SSS+1];
    Real  temp2[4*SSS+1];
    Real     Tp[4*SSS+1][4*SSS+1];
    Real    Tpp[4*SSS+1][4*SSS+1];
    Real     aj[4*SSS+1];
    Real     cj[4*SSS+1];
    Real     ej[4*SSS+1];
    Real   Tpaj[4*SSS+1];
    Real   Tpcj[4*SSS+1];
    Real  Tppaj[4*SSS+1];
    Real      G[4*SSS+1][4*SSS+1];    // Extracted from first 4*SSS+1 columns of Gg[][].  indexed as [row][col]
    Real      g[4*SSS+1];             // Extracted from last [4*SSS+1] column of Gg[][].
    Real     Gg[(4*SSS+1)*(4*SSS+2)]; // Buffer to hold the Gram-like matrix produced by matmul().  indexed as [row*(4*SSS+2) + col]

    __zero(   aj, 4*SSS+1);
    __zero(   cj, 4*SSS+1);
    __zero(   ej, 4*SSS+1);
    __zero( Tpaj, 4*SSS+1);
    __zero( Tpcj, 4*SSS+1);
    __zero(Tppaj, 4*SSS+1);
    __zero(temp1, 4*SSS+1);
    __zero(temp2, 4*SSS+1);
    //
    // Initialize Tp[][] and Tpp[][] ...
    //
    // This is the monomial basis stuff.
    //
    for (int i = 0; i < 4*SSS+1; i++)
        for (int j = 0; j < 4*SSS+1; j++) Tp[i][j]   = 0;
    for (int i = 0;       i < 2*SSS; i++) Tp[i+1][i] = 1;
    for (int i = 2*SSS+1; i < 4*SSS; i++) Tp[i+1][i] = 1;

    for (int i = 0; i < 4*SSS+1; i++)
        for (int j = 0; j < 4*SSS+1; j++)   Tpp[i][j]   = 0;
    for (int i = 0;       i < 2*SSS-1; i++) Tpp[i+2][i] = 1;
    for (int i = 2*SSS+1; i < 4*SSS-1; i++) Tpp[i+2][i] = 1;

    const int ncomp  = 1, nghost = 1;

    MultiFab  p(sol.boxArray(), ncomp, nghost);
    MultiFab  r(sol.boxArray(), ncomp, nghost);
    MultiFab rt(sol.boxArray(), ncomp, nghost);
    //
    // Contains the matrix powers of p[] and r[].
    //
    // First 2*SSS+1 components are powers of p[].
    // Next  2*SSS   components are powers of r[].
    //
    MultiFab PR(sol.boxArray(), 4*SSS+1, nghost);

    MultiFab tmp(sol.boxArray(), 4, nghost);

    Lp.residual(r, rhs, sol, lev, bc_mode);

    MultiFab::Copy(rt,r,0,0,1,0);
    MultiFab::Copy( p,r,0,0,1,0);

    const Real           rnorm0        = norm_inf(r);
    Real                 delta         = dotxy(r,rt);
    const Real           L2_norm_of_rt = sqrt(delta);
    const LinOp::BC_Mode temp_bc_mode  = LinOp::Homogeneous_BC;

    if ( verbose > 0 && ParallelDescriptor::IOProcessor() )
    {
        Spacer(std::cout, lev);
        std::cout << "CGSolver_CABiCGStab: Initial error (error0) =        " << rnorm0 << '\n';
    }

    if ( rnorm0 == 0 || delta == 0 || rnorm0 < eps_abs )
    {
        if ( verbose > 0 && ParallelDescriptor::IOProcessor() )
	{
            Spacer(std::cout, lev);
            std::cout << "CGSolver_CABiCGStab: niter = 0,"
                      << ", rnorm = "   << rnorm0
                      << ", delta = "   << delta
                      << ", eps_abs = " << eps_abs << '\n';
	}
        return 0;
    }

    int niters = 0, ret = 0;

    bool BiCGStabFailed = false, BiCGStabConverged = false;

    for (int m = 0; m < maxiter && !BiCGStabFailed && !BiCGStabConverged; m += SSS)
    {
        //
        // Compute the matrix powers on p[] & r[] (monomial basis).
        // The 2*SSS+1 powers of p[] followed by the 2*SSS powers of r[].
        //
        MultiFab::Copy(PR,p,0,0,1,0);
        MultiFab::Copy(PR,r,0,2*SSS+1,1,0);
        //
        // We use "tmp" to minimize the number of Lp.apply()s.
        // We do this by doing p & r together in a single call.
        //
        MultiFab::Copy(tmp,p,0,0,1,0);
        MultiFab::Copy(tmp,r,0,1,1,0);

        for (int n = 1; n < 2*SSS; n++)
        {
            Lp.apply(tmp, tmp, lev, temp_bc_mode, false, 0, 2, 2);

            MultiFab::Copy(tmp,tmp,2,0,2,0);

            MultiFab::Copy(PR,tmp,0,        n,1,0);
            MultiFab::Copy(PR,tmp,1,2*SSS+n+1,1,0);
        }

        Lp.apply(PR, PR, lev, temp_bc_mode, false, 2*SSS-1, 2*SSS, 1);

        BuildGramMatrix(Gg, PR, rt);
        //
        // Form G[][] and g[] from Gg.
        //
        for (int i = 0, k = 0; i < 4*SSS+1; i++)
        {
            for (int j = 0; j < 4*SSS+1; j++)
                //
                // First 4*SSS+1 elements in each row go to G[][].
                //
                G[i][j] = Gg[k++];
            //
            // Last element in row goes to g[].
            //
            g[i] = Gg[k++];
        }

        __zero(aj, 4*SSS+1); aj[0]       = 1;
        __zero(cj, 4*SSS+1); cj[2*SSS+1] = 1;
        __zero(ej, 4*SSS+1);

        int nit = 0;

        for ( ; nit < SSS; nit++)
        {
            __gemv( Tpaj, 1.0,  Tp, aj, 0.0,  Tpaj, 4*SSS+1, 4*SSS+1);
            __gemv( Tpcj, 1.0,  Tp, cj, 0.0,  Tpcj, 4*SSS+1, 4*SSS+1);
            __gemv(Tppaj, 1.0, Tpp, aj, 0.0, Tppaj, 4*SSS+1, 4*SSS+1);

            const Real g_dot_Tpaj = __dot(g, Tpaj, 4*SSS+1);

            if ( g_dot_Tpaj == 0 )
            {
                if ( verbose > 1 && ParallelDescriptor::IOProcessor() )
                    std::cout << "CGSolver_CABiCGStab: g_dot_Tpaj == 0, nit = " << nit << '\n';
                BiCGStabFailed = true; ret = 1; break;
            }

            const Real alpha = delta / g_dot_Tpaj;

            __axpy(temp1, 1.0,     Tpcj,-alpha, Tppaj, 4*SSS+1);         //  temp1[] =  (T'cj - alpha*T''aj)
            __gemv(temp2, 1.0,  G,temp1,   0.0, temp2, 4*SSS+1,4*SSS+1); //  temp2[] = G(T'cj - alpha*T''aj)
            __axpy(temp1, 1.0,       cj,-alpha,  Tpaj, 4*SSS+1);         //  temp1[] =     cj - alpha*T'aj

            const Real omega_numerator = __dot(temp1, temp2, 4*SSS+1); //  (temp1,temp2) = ( (cj - alpha*T'aj) , G(T'cj - alpha*T''aj) )

            __axpy(temp1, 1.0,     Tpcj,-alpha, Tppaj, 4*SSS+1);         //  temp1[] =  (T'cj - alpha*T''aj)
            __gemv(temp2, 1.0,  G,temp1,   0.0, temp2, 4*SSS+1,4*SSS+1); //  temp2[] = G(T'cj - alpha*T''aj)

            const Real omega_denominator = __dot(temp1,temp2,4*SSS+1); //  (temp1,temp2) = ( (T'cj - alpha*T''aj) , G(T'cj - alpha*T''aj) )

            __axpy(ej, 1.0, ej, alpha, aj, 4*SSS+1);

            if ( omega_denominator == 0 )
            {
                if ( verbose > 1 && ParallelDescriptor::IOProcessor() )
                    std::cout << "CGSolver_CABiCGStab: omega_denominator == 0, nit = " << nit << '\n';
                BiCGStabFailed = true; ret = 2; break;
            }

            const Real omega = omega_numerator / omega_denominator;

            if ( omega == 0 )
            {
                if ( verbose > 1 && ParallelDescriptor::IOProcessor() )
                    if ( omega == 0 ) std::cout << "CGSolver_CABiCGStab: omega == 0, nit = " << nit << '\n';
                BiCGStabFailed = true; ret = 3; break;
            }

            __axpy(ej, 1.0, ej,       omega,    cj, 4*SSS+1);
            __axpy(ej, 1.0, ej,-omega*alpha,  Tpaj, 4*SSS+1);
            __axpy(cj, 1.0, cj,      -omega,  Tpcj, 4*SSS+1);
            __axpy(cj, 1.0, cj,      -alpha,  Tpaj, 4*SSS+1);
            __axpy(cj, 1.0, cj, omega*alpha, Tppaj, 4*SSS+1);
            //
            // Do an early check of the residual to determine convergence.
            //
            __gemv(temp1, 1.0, G, cj, 0.0, temp1, 4*SSS+1, 4*SSS+1);
            //
            // sqrt( (cj,Gcj) ) == L2 norm of the intermediate residual in exact arithmetic.
            // However, finite precision can lead to the norm^2 being < 0 (Jim Demmel).
            // If cj_dot_Gcj < 0 we flush to zero and consider ourselves converged.
            //
            const Real cj_dot_Gcj          = __dot(cj, temp1, 4*SSS+1);
            const Real L2_norm_of_residual = (cj_dot_Gcj > 0 ? sqrt(cj_dot_Gcj) : 0);

            if ( L2_norm_of_residual < eps_rel*L2_norm_of_rt )
            {
                BiCGStabConverged = true; break;
            }

            const Real delta_next = __dot(g, cj, 4*SSS+1);
            const Real beta       = (delta_next/delta)*(alpha/omega);

            if ( delta_next == 0 )
            {
                if ( __dot(cj,cj,4*SSS+1) == 0 )
                {
                    BiCGStabConverged = true;
                }
                else
                {
                    if ( verbose > 1 && ParallelDescriptor::IOProcessor() )
                        std::cout << "CGSolver_CABiCGStab: delta_next == 0, nit = " << nit << '\n';
                    BiCGStabFailed = true; ret = 4;
                }
                break;
            }

            __axpy(aj, 1.0, cj,        beta,   aj, 4*SSS+1);
            __axpy(aj, 1.0, aj, -omega*beta, Tpaj, 4*SSS+1);

            delta = delta_next;
        }
        //
        // Update iterates. Always update the solution.
        // No need to update p or r if we "failed".
        //
        for (int i = 0; i < 4*SSS+1; i++)
            sxay(sol,sol,ej[i],PR,i);

        if ( !BiCGStabFailed )
        {
            MultiFab::Copy(p,PR,0,0,1,0);
            p.mult(aj[0],0,1);
            for (int i = 1; i < 4*SSS+1; i++)
                sxay(p,p,aj[i],PR,i);

            MultiFab::Copy(r,PR,0,0,1,0);
            r.mult(cj[0],0,1);
            for (int i = 1; i < 4*SSS+1; i++)
                sxay(r,r,cj[i],PR,i);
        }

        niters += nit;
    }

    Real rnorm = -1;

    if ( verbose > 0 )
    {
        rnorm = norm_inf(r);

        if ( ParallelDescriptor::IOProcessor() )
        {
            Spacer(std::cout, lev);
            std::cout << "CGSolver_CABiCGStab: Final: Iteration "
                      << std::setw(4) << niters
                      << " rel. err. "
                      << rnorm/rnorm0 << '\n';
        }
    }

    if ( niters == maxiter && ret == 0 && !BiCGStabConverged)
    {
        if ( rnorm < 0) rnorm = norm_inf(r);

        if ( rnorm > rnorm0 )
        {
            if ( ParallelDescriptor::IOProcessor() )
                BoxLib::Warning("CGSolver_CABiCGStab: failed to converge!");
            ret = 8;
        }
        else
        {
            ret = 5;
        }
    }

    return ret;
}

int
CGSolver::solve_bicgstab (MultiFab&       sol,
                          const MultiFab& rhs,
                          Real            eps_rel,
                          Real            eps_abs,
                          LinOp::BC_Mode  bc_mode)
{
    BL_PROFILE("CGSolver::solve_bicgstab()");

    const int nghost = 1, ncomp = 1;

    BL_ASSERT(sol.nComp() == ncomp);
    BL_ASSERT(sol.boxArray() == Lp.boxArray(lev));
    BL_ASSERT(rhs.boxArray() == Lp.boxArray(lev));

    MultiFab sorig(sol.boxArray(), ncomp, nghost);
    MultiFab s(sol.boxArray(), ncomp, nghost);
    MultiFab sh(sol.boxArray(), ncomp, nghost);
    MultiFab r(sol.boxArray(), ncomp, nghost);
    MultiFab rh(sol.boxArray(), ncomp, nghost);
    MultiFab p(sol.boxArray(), ncomp, nghost);
    MultiFab ph(sol.boxArray(), ncomp, nghost);
    MultiFab v(sol.boxArray(), ncomp, nghost);
    MultiFab t(sol.boxArray(), ncomp, nghost);

    MultiFab::Copy(sorig,sol,0,0,1,0);

    Lp.residual(r, rhs, sorig, lev, bc_mode);

    MultiFab::Copy(rh,r,0,0,1,0);

    sol.setVal(0);

    const LinOp::BC_Mode temp_bc_mode = LinOp::Homogeneous_BC;

#ifdef CG_USE_OLD_CONVERGENCE_CRITERIA
    Real rnorm = norm_inf(r);
#else
    //
    // Calculate the local values of these norms & reduce their values together.
    //
    Real vals[2] = { norm_inf(r, true), Lp.norm(0, lev, true) };

    ParallelDescriptor::ReduceRealMax(&vals[0],2);

    Real       rnorm    = vals[0];
    const Real Lp_norm  = vals[1];
    Real       sol_norm = 0;
#endif
    const Real rnorm0   = rnorm;

    if (verbose > 0 && ParallelDescriptor::IOProcessor())
    {
        Spacer(std::cout, lev);
        std::cout << "CGSolver_BiCGStab: Initial error (error0) =        " << rnorm0 << '\n';
    }
    int ret = 0, nit = 1;
    Real rho_1 = 0, alpha = 0, omega = 0;

    if ( rnorm0 == 0 || rnorm0 < eps_abs )
    {
        if (verbose > 0 && ParallelDescriptor::IOProcessor())
	{
            Spacer(std::cout, lev);
            std::cout << "CGSolver_BiCGStab: niter = 0,"
                      << ", rnorm = " << rnorm 
                      << ", eps_abs = " << eps_abs << std::endl;
	}
        return ret;
    }

    for (; nit <= maxiter; ++nit)
    {
        const Real rho = dotxy(rh,r);
        if ( rho == 0 ) 
	{
            ret = 1; break;
	}
        if ( nit == 1 )
        {
            MultiFab::Copy(p,r,0,0,1,0);
        }
        else
        {
            const Real beta = (rho/rho_1)*(alpha/omega);
            sxay(p, p, -omega, v);
            sxay(p, r,   beta, p);
        }
        if ( use_mg_precond )
        {
            ph.setVal(0);
            mg_precond->solve(ph, p, eps_rel, eps_abs, temp_bc_mode);
        }
        else if ( use_jacobi_precond )
        {
            ph.setVal(0);
            Lp.jacobi_smooth(ph, p, lev, temp_bc_mode);
        }
        else 
        {
            MultiFab::Copy(ph,p,0,0,1,0);
        }
        Lp.apply(v, ph, lev, temp_bc_mode);

        if ( Real rhTv = dotxy(rh,v) )
	{
            alpha = rho/rhTv;
	}
        else
	{
            ret = 2; break;
	}
        sxay(sol, sol,  alpha, ph);
        sxay(s,     r, -alpha,  v);

        rnorm = norm_inf(s);

        if ( ParallelDescriptor::IOProcessor() )
        {
            if (verbose > 1 ||
                (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
                  (eps_abs > 0. && rnorm < eps_abs)) && verbose))
            {
                Spacer(std::cout, lev);
                std::cout << "CGSolver_BiCGStab: Half Iter "
                          << std::setw(11) << nit
                          << " rel. err. "
                          << rnorm/(rnorm0) << '\n';
            }
        }

#ifdef CG_USE_OLD_CONVERGENCE_CRITERIA
        if ( rnorm < eps_rel*rnorm0 || rnorm < eps_abs ) break;
#else
        sol_norm = norm_inf(sol);
        if ( rnorm < eps_rel*(Lp_norm*sol_norm + rnorm0 ) || rnorm < eps_abs ) break;
#endif
        if ( use_mg_precond )
        {
            sh.setVal(0);
            mg_precond->solve(sh, s, eps_rel, eps_abs, temp_bc_mode);
        }
        else if ( use_jacobi_precond )
        {
            sh.setVal(0);
            Lp.jacobi_smooth(sh, s, lev, temp_bc_mode);
        }
        else
        {
            MultiFab::Copy(sh,s,0,0,1,0);
        }
        Lp.apply(t, sh, lev, temp_bc_mode);
        //
        // This is a little funky.  I want to elide one of the reductions
        // in the following two dotxy()s.  We do that by calculating the "local"
        // values and then reducing the two local values at the same time.
        //
        Real vals[2] = { dotxy(t,t,true), dotxy(t,s,true) };

        ParallelDescriptor::ReduceRealSum(&vals[0],2);

        if ( vals[0] )
	{
            omega = vals[1]/vals[0];
	}
        else
	{
            ret = 3; break;
	}
        sxay(sol, sol,  omega, sh);
        sxay(r,     s, -omega,  t);

        rnorm = norm_inf(r);

        if (ParallelDescriptor::IOProcessor())
        {
            if (verbose > 1 ||
                (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
                  (eps_abs > 0. && rnorm < eps_abs)) && verbose))
            {
                Spacer(std::cout, lev);
                std::cout << "CGSolver_BiCGStab: Iteration "
                          << std::setw(11) << nit
                          << " rel. err. "
                          << rnorm/(rnorm0) << '\n';
            }
        }

#ifdef CG_USE_OLD_CONVERGENCE_CRITERIA
        if ( rnorm < eps_rel*rnorm0 || rnorm < eps_abs ) break;
#else
        sol_norm = norm_inf(sol);
        if ( rnorm < eps_rel*(Lp_norm*sol_norm + rnorm0 ) || rnorm < eps_abs ) break;
#endif
        if ( omega == 0 )
	{
            ret = 4; break;
	}
        rho_1 = rho;
    }

    if (ParallelDescriptor::IOProcessor())
    {
        if (verbose > 0 ||
            (((eps_rel > 0 && rnorm < eps_rel*rnorm0) ||
              (eps_abs > 0 && rnorm < eps_abs)) && verbose))
        {
            Spacer(std::cout, lev);
            std::cout << "CGSolver_BiCGStab: Final: Iteration "
                      << std::setw(4) << nit
                      << " rel. err. "
                      << rnorm/(rnorm0) << '\n';
        }
    }

#ifdef CG_USE_OLD_CONVERGENCE_CRITERIA
    if ( ret == 0 && rnorm > eps_rel*rnorm0 && rnorm > eps_abs)
#else
    if ( ret == 0 && rnorm > eps_rel*(Lp_norm*sol_norm + rnorm0 ) && rnorm > eps_abs )
#endif
    {
        if ( ParallelDescriptor::IOProcessor() )
            BoxLib::Warning("CGSolver_BiCGStab:: failed to converge!");
        ret = 8;
    }

    if ( ( ret == 0 || ret == 8 ) && (rnorm < rnorm0) )
    {
        sol.plus(sorig, 0, 1, 0);
    } 
    else 
    {
        sol.setVal(0);
        sol.plus(sorig, 0, 1, 0);
    }

    return ret;
}

int
CGSolver::solve_cg (MultiFab&       sol,
		    const MultiFab& rhs,
		    Real            eps_rel,
		    Real            eps_abs,
		    LinOp::BC_Mode  bc_mode)
{
    BL_PROFILE("CGSolver::solve_cg()");

    const int nghost = 1, ncomp = 1;

    BL_ASSERT(sol.nComp() == ncomp);
    BL_ASSERT(sol.boxArray() == Lp.boxArray(lev));
    BL_ASSERT(rhs.boxArray() == Lp.boxArray(lev));

    MultiFab sorig(sol.boxArray(), ncomp, nghost);
    MultiFab r(sol.boxArray(), ncomp, nghost);
    MultiFab z(sol.boxArray(), ncomp, nghost);
    MultiFab q(sol.boxArray(), ncomp, nghost);
    MultiFab p(sol.boxArray(), ncomp, nghost);

    MultiFab r1(sol.boxArray(), ncomp, nghost);
    MultiFab z1(sol.boxArray(), ncomp, nghost);
    MultiFab r2(sol.boxArray(), ncomp, nghost);
    MultiFab z2(sol.boxArray(), ncomp, nghost);

    MultiFab::Copy(sorig,sol,0,0,1,0);

    Lp.residual(r, rhs, sorig, lev, bc_mode);

    sol.setVal(0);

    const LinOp::BC_Mode temp_bc_mode=LinOp::Homogeneous_BC;

    Real       rnorm    = norm_inf(r);
    const Real rnorm0   = rnorm;
    Real       minrnorm = rnorm;

    if (verbose > 0 && ParallelDescriptor::IOProcessor())
    {
        Spacer(std::cout, lev);
        std::cout << "              CG: Initial error :        " << rnorm0 << '\n';
    }

    const Real Lp_norm = Lp.norm(0, lev);
    Real sol_norm      = 0;
    Real rho_1         = 0;
    int  ret           = 0;
    int  nit           = 1;

    if ( rnorm == 0 || rnorm < eps_abs )
    {
        if (verbose > 0 && ParallelDescriptor::IOProcessor())
	{
            Spacer(std::cout, lev);
            std::cout << "       CG: niter = 0,"
                      << ", rnorm = " << rnorm 
                      << ", eps_rel*(Lp_norm*sol_norm + rnorm0 )" <<  eps_rel*(Lp_norm*sol_norm + rnorm0 ) 
                      << ", eps_abs = " << eps_abs << std::endl;
	}
        return 0;
    }

    for (; nit <= maxiter; ++nit)
    {
        if (use_jbb_precond && ParallelDescriptor::NProcs() > 1)
        {
            z.setVal(0);

            jbb_precond(z,r,lev,Lp);
        }
        else
        {
            MultiFab::Copy(z,r,0,0,1,0);
        }

        Real rho = dotxy(z,r);

        if (nit == 1)
        {
            MultiFab::Copy(p,z,0,0,1,0);
        }
        else
        {
            Real beta = rho/rho_1;
            sxay(p, z, beta, p);
        }
        Lp.apply(q, p, lev, temp_bc_mode);

        Real alpha;
        if ( Real pw = dotxy(p,q) )
	{
            alpha = rho/pw;
	}
        else
	{
            ret = 1; break;
	}
        
        if (ParallelDescriptor::IOProcessor() && verbose > 2)
        {
            Spacer(std::cout, lev);
            std::cout << "CGSolver_cg:"
                      << " nit " << nit
                      << " rho " << rho
                      << " alpha " << alpha << '\n';
        }
        sxay(sol, sol, alpha, p);
        sxay(  r,   r,-alpha, q);
        rnorm = norm_inf(r);
        sol_norm = norm_inf(sol);

        if (ParallelDescriptor::IOProcessor())
        {
            if (verbose > 1 ||
                (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
                  (eps_abs > 0. && rnorm < eps_abs)) && verbose))
            {
                Spacer(std::cout, lev);
                std::cout << "       CG:       Iteration"
                          << std::setw(4) << nit
                          << " rel. err. "
                          << rnorm/(rnorm0) << '\n';
            }
        }

#ifdef CG_USE_OLD_CONVERGENCE_CRITERIA
        if ( rnorm < eps_rel*rnorm0 || rnorm < eps_abs ) break;
#else
        if ( rnorm < eps_rel*(Lp_norm*sol_norm + rnorm0) || rnorm < eps_abs ) break;
#endif
        if ( rnorm > def_unstable_criterion*minrnorm )
	{
            ret = 2; break;
	}
        else if ( rnorm < minrnorm )
	{
            minrnorm = rnorm;
	}

        rho_1 = rho;
    }
    
    if (ParallelDescriptor::IOProcessor())
    {
        if (verbose > 0 ||
            (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
              (eps_abs > 0. && rnorm < eps_abs)) && verbose))
	{
            Spacer(std::cout, lev);
            std::cout << "       CG: Final Iteration"
                      << std::setw(4) << nit
                      << " rel. err. "
                      << rnorm/(rnorm0) << '\n';
	}
    }

#ifdef CG_USE_OLD_CONVERGENCE_CRITERIA
    if ( ret == 0 &&  rnorm > eps_rel*rnorm0 && rnorm > eps_abs )
#else
    if ( ret == 0 && rnorm > eps_rel*(Lp_norm*sol_norm + rnorm0) && rnorm > eps_abs )
#endif
    {
        if ( ParallelDescriptor::IOProcessor() )
            BoxLib::Warning("CGSolver_cg: failed to converge!");
        ret = 8;
    }

    if ( ( ret == 0 || ret == 8 ) && (rnorm < rnorm0) )
    {
        sol.plus(sorig, 0, 1, 0);
    } 
    else 
    {
        sol.setVal(0);
        sol.plus(sorig, 0, 1, 0);
    }

    return ret;
}

int
CGSolver::jbb_precond (MultiFab&       sol,
		       const MultiFab& rhs,
                       int             lev,
		       LinOp&          Lp)
{
    //
    // This is a local routine.  No parallel is allowed to happen here.
    //
    int                  lev_loc = lev;
    const Real           eps_rel = 1.e-2;
    const Real           eps_abs = 1.e-16;
    const int            nghost  = 1;
    const int            ncomp   = sol.nComp();
    const bool           local   = true;
    const LinOp::BC_Mode bc_mode = LinOp::Homogeneous_BC;

    BL_ASSERT(ncomp == 1 );
    BL_ASSERT(sol.boxArray() == Lp.boxArray(lev_loc));
    BL_ASSERT(rhs.boxArray() == Lp.boxArray(lev_loc));

    MultiFab sorig(sol.boxArray(), ncomp, nghost);

    MultiFab r(sol.boxArray(), ncomp, nghost);
    MultiFab z(sol.boxArray(), ncomp, nghost);
    MultiFab q(sol.boxArray(), ncomp, nghost);
    MultiFab p(sol.boxArray(), ncomp, nghost);

    sorig.copy(sol);

    Lp.residual(r, rhs, sorig, lev_loc, LinOp::Homogeneous_BC, local);

    sol.setVal(0);

    Real       rnorm    = norm_inf(r,local);
    const Real rnorm0   = rnorm;
    Real       minrnorm = rnorm;

    if (verbose > 2 && ParallelDescriptor::IOProcessor())
    {
        Spacer(std::cout, lev_loc);
        std::cout << "     jbb_precond: Initial error :        " << rnorm0 << '\n';
    }

    const Real Lp_norm = Lp.norm(0, lev_loc, local);
    Real sol_norm = 0;
    int  ret      = 0;			// will return this value if all goes well
    Real rho_1    = 0;
    int  nit      = 1;

    if ( rnorm0 == 0 || rnorm0 < eps_abs )
    {
        if (verbose > 2 && ParallelDescriptor::IOProcessor())
	{
            Spacer(std::cout, lev_loc);
            std::cout << "jbb_precond: niter = 0,"
                      << ", rnorm = " << rnorm 
                      << ", eps_abs = " << eps_abs << std::endl;
	}
        return 0;
    }

    for (; nit <= maxiter; ++nit)
    {
        z.copy(r);

        Real rho = dotxy(z,r,local);
        if (nit == 1)
        {
            p.copy(z);
        }
        else
        {
            Real beta = rho/rho_1;
            sxay(p, z, beta, p);
        }

        Lp.apply(q, p, lev_loc, bc_mode, local);

        Real alpha;
        if ( Real pw = dotxy(p,q,local) )
	{
            alpha = rho/pw;
	}
        else
	{
            ret = 1; break;
	}
        
        if ( ParallelDescriptor::IOProcessor() && verbose > 3 )
        {
            Spacer(std::cout, lev_loc);
            std::cout << "jbb_precond:" << " nit " << nit
                      << " rho " << rho << " alpha " << alpha << '\n';
        }
        sxay(sol, sol, alpha, p);
        sxay(  r,   r,-alpha, q);
        rnorm    = norm_inf(r,   local);
        sol_norm = norm_inf(sol, local);

        if ( ParallelDescriptor::IOProcessor() )
        {
            if ( verbose > 3 ||
                 (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
                   (eps_abs > 0. && rnorm < eps_abs)) && verbose > 3) )
            {
                Spacer(std::cout, lev_loc);
                std::cout << "jbb_precond:       Iteration"
                          << std::setw(4) << nit
                          << " rel. err. "
                          << rnorm/(rnorm0) << '\n';
            }
        }

        if ( rnorm < eps_rel*(Lp_norm*sol_norm + rnorm0) || rnorm < eps_abs )
	{
            break;
	}
      
        if ( rnorm > def_unstable_criterion*minrnorm )
	{
            ret = 2; break;
	}
        else if ( rnorm < minrnorm )
	{
            minrnorm = rnorm;
	}

        rho_1 = rho;
    }
    
    if ( ParallelDescriptor::IOProcessor() )
    {
        if ( verbose > 2 ||
             (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
               (eps_abs > 0. && rnorm < eps_abs)) && (verbose > 2)) )
	{
            Spacer(std::cout, lev_loc);
            std::cout << "jbb_precond: Final Iteration"
                      << std::setw(4) << nit
                      << " rel. err. "
                      << rnorm/(rnorm0) << '\n';
	}
    }
    if ( ret == 0 && rnorm > eps_rel*(Lp_norm*sol_norm + rnorm0) && rnorm > eps_abs )
    {
        if ( ParallelDescriptor::IOProcessor() )
	{
            BoxLib::Warning("jbb_precond:: failed to converge!");
	}
        ret = 8;
    }

    if ( ( ret == 0 || ret == 8 ) && (rnorm < rnorm0) )
    {
        sol.plus(sorig, 0, 1, 0);
    } 
    else 
    {
        sol.setVal(0);
        sol.plus(sorig, 0, 1, 0);
    }

    return ret;
}
