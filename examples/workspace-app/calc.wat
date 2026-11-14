;; Documentation source for the checked-in calc.wasm; not needed by the build.
(module
  (func (export "add") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.add))
