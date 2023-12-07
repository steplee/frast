# WebGpu Target (Nov 2023)
I'd like to add support for both native & in-browser rendering.

The native client should use Google's Dawn WebGpu implementation.
The in-browser code should use emscripten.

The native client could use familiar in-process message passing to load tiles.
The in-browser code should use HTTP requests.

I'll have to learn a bunch to complete this project, which is why I'm excited for it.
I have not worked with WASM yet, nor WebGpu.
I think I'll run deeper into JS async code than I've had to, and need to use WebWorkers.
