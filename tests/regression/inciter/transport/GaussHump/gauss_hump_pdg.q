# vim: filetype=sh:
# This is a comment
# Keywords are case-sensitive

title "Advection of 2D Gaussian hump"

inciter

  nstep 50  # Max number of time steps
  dt   2.0e-4 # Time step size
  ttyi 5     # TTY output interval
  scheme pdg

  transport
    physics advection
    problem gauss_hump
    ncomp 1
    depvar c

    bc_extrapolate
      sideset 1 end
    end
    bc_dirichlet
      sideset 2 end
    end
    bc_outlet
      sideset 3 end
    end
  end

  pref
    tolref 0.1
  end

  diagnostics
    interval  10
    format    scientific
    error l2
    error linf
  end

  plotvar
    interval 10
  end

end
