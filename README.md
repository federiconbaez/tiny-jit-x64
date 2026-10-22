# tiny-jit-x64

Un JIT mínimo para expresiones enteras: parsea `x*(y+1) - 3*4`, pliega constantes, emite código máquina x86-64 a mano (sin ensamblador ni bibliotecas) y lo ejecuta desde una página W^X.

A minimal JIT for integer expressions: it parses `x*(y+1) - 3*4`, folds constants, hand-emits x86-64 machine code (no assembler, no libraries) and runs it from a W^X page.

```
$ ./build/tjit --hex 'x*(y+1) - 3*4' 6 6
36 bytes: 48 89 f8 50 48 89 f0 48 c7 c1 01 00 00 00 48 01 c8 48 89 c1 58 48 0f af c1 48 c7 c1 0c 00 00 00 48 29 c8 c3
30
```

## Español

**Qué hace**

- Lenguaje: enteros de 64 bits con signo, variables `x`, `y`, `z` (los tres argumentos System V: `rdi`, `rsi`, `rdx`), `+ - *`, menos unario y paréntesis. La aritmética envuelve (módulo 2^64), igual que el hardware; el intérprete la reproduce con aritmética sin signo, así que no hay UB.
- Parser descendente recursivo con errores con offset; límites de profundidad (200) y de nodos (10 000) para que una entrada hostil no agote la pila. Acepta `-9223372036854775808`.
- Plegado: constantes, `a+0`, `a*1`, `a*0`, `a-0`, `--a`.
- Emisión: máquina de pila sobre `rax`/`rcx` con `push`/`pop`; si el operando derecho es una hoja se carga directo en `rcx` sin tocar la pila. Constantes: `xor eax,eax`, `mov r64, imm32` (con extensión de signo) o `movabs`.
- Memoria: `mmap` RW, copia, relleno con `int3`, `mprotect` a RX. La página nunca es escribible y ejecutable a la vez; se libera con `munmap` al destruir el `ExecBuffer` (movible, no copiable).

**Pruebas** (`ctest`)

- Golden de bytes para 15 expresiones (cada codificación usada, con y sin plegado).
- Diferencial: 3000 expresiones aleatorias (constantes en los bordes de imm32 e int64) × 4 entradas → 12 000 evaluaciones comparando intérprete, JIT y JIT plegado.
- 8 errores de parseo con su offset exacto, más los límites de profundidad y tamaño.
- W^X: lee `/proc/self/maps` y exige `r-x` en la región; verifica el relleno `int3`, la semántica de movimiento y que la región desaparece al destruirse.
- Mutación de control: cambiar el opcode de `sub` por el de `add` produce 4945 fallos.
- CI: x86-64 con g++ y clang++, con y sin ASan+UBSan; arm64 compila y corre todo excepto la ejecución del código (solo emite).

**Un hallazgo real**: con clang y `-fsanitize=function` (parte de UBSan), una llamada indirecta lee 8 bytes *antes* del destino buscando la firma del tipo. El código JIT no la tiene y la página anterior puede no estar mapeada → SEGV. Por eso `ExecBuffer::call()` desactiva ese chequeo solo en ese punto; `fn()` sigue disponible para quien llame sin sanitizers.

**Benchmark** (`bench_tjit`, una expresión de 37 nodos, mediana de 7 corridas de 5 M llamadas, VM Intel Xeon de 8 vCPU, Release):

| variante | ns/llamada |
|---|---|
| intérprete de árbol | 66.3 |
| JIT (201 bytes) | 3.4 |
| JIT plegado (25 nodos, 127 bytes) | 2.8 |
| la misma expresión compilada por g++ -O2 e inlineada | 0.8 |

La última fila es una cota inferior, no una comparación justa: no paga la llamada indirecta. El JIT no asigna registros; el código usa la pila para cada operando derecho no trivial.

**Qué no es**: no hay división (evita el caso de trap de `INT64_MIN / -1`), ni asignación de registros, ni soporte arm64 para ejecutar.

## English

**What it does**

- Language: signed 64-bit integers, variables `x`, `y`, `z` (the three System V arguments: `rdi`, `rsi`, `rdx`), `+ - *`, unary minus and parentheses. Arithmetic wraps (mod 2^64) like the hardware; the interpreter reproduces this with unsigned arithmetic, so there is no UB.
- Recursive-descent parser with offset-carrying errors; depth (200) and node (10,000) limits so hostile input cannot exhaust the stack. Accepts `-9223372036854775808`.
- Folding: constants, `a+0`, `a*1`, `a*0`, `a-0`, `--a`.
- Emission: stack machine over `rax`/`rcx` with `push`/`pop`; a leaf right operand goes straight into `rcx` without touching the stack. Constants: `xor eax,eax`, sign-extended `mov r64, imm32`, or `movabs`.
- Memory: `mmap` RW, copy, pad with `int3`, `mprotect` to RX. The page is never writable and executable at once; it is `munmap`ed when the (movable, non-copyable) `ExecBuffer` is destroyed.

**Tests** (`ctest`)

- Byte goldens for 15 expressions (every encoding used, with and without folding).
- Differential: 3000 random expressions (constants at imm32 and int64 edges) × 4 inputs → 12,000 evaluations comparing interpreter, JIT and folded JIT.
- 8 parse errors with their exact offset, plus the depth and size limits.
- W^X: reads `/proc/self/maps` and requires `r-x` for the region; checks the `int3` padding, move semantics and that the region is gone after destruction.
- Control mutation: swapping the `sub` opcode for `add` yields 4945 failures.
- CI: x86-64 with g++ and clang++, with and without ASan+UBSan; arm64 builds and runs everything except executing the code (emit only).

**A real finding**: with clang and `-fsanitize=function` (part of UBSan), an indirect call reads 8 bytes *before* the target looking for a type signature. JIT code has none and the preceding page may be unmapped → SEGV. So `ExecBuffer::call()` disables that check at that one call site; `fn()` is still there for callers without sanitizers.

**Benchmark** (`bench_tjit`, one 37-node expression, median of 7 runs of 5 M calls, 8-vCPU Intel Xeon VM, Release):

| variant | ns/call |
|---|---|
| tree interpreter | 66.3 |
| JIT (201 bytes) | 3.4 |
| folded JIT (25 nodes, 127 bytes) | 2.8 |
| same expression compiled by g++ -O2 and inlined | 0.8 |

The last row is a lower bound, not a fair comparison: it does not pay for the indirect call. The JIT does no register allocation; every non-trivial right operand goes through the stack.

**What it is not**: no division (avoids the `INT64_MIN / -1` trap case), no register allocation, no arm64 execution.

## Build

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # -DTJIT_SANITIZE=ON for ASan+UBSan
cmake --build build && ctest --test-dir build
./build/tjit [--hex] [--no-fold] 'EXPR' [x [y [z]]]
./build/bench_tjit
```

## License

Apache-2.0
