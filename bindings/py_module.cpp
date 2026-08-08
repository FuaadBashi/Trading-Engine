// Slice 4+. pybind11 module: run_backtest(capture, params) -> numpy arrays.
// Release the GIL around the backtest call or parameter sweeps will not parallelise.
// TODO(fuaad): write this yourself.
