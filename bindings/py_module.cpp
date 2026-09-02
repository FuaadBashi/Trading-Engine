// Reserved for plan v4 §16-17 (Stage 6-7). pybind11 module: run_backtest(capture, params) ->
// numpy arrays, feeding Python-side label/research work.
// Release the GIL around the backtest call or parameter sweeps will not parallelise.
// TODO(fuaad): write this yourself.
