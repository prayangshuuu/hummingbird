# Release Notes: RFC-019 (Runtime Orchestrator)

We are thrilled to announce the successful implementation of RFC-019, marking the delivery of Hummingbird's first end-to-end inference path!

## Key Features
- **Runtime Orchestrator (`src/runtime`)**: The apex module integrating the entire stack, managing the generation loop, state transitions, and memory cleanup.
- **Tokenizer Integration**: Seamless encoding of string prompts and decoding of generated tokens.
- **End-to-End CPU Execution**: Complete deterministic generation of tokens via the scalar CPU backend.
- **Stable APIs**: `hbi_runtime_session_create`, `hbi_runtime_generate`, and safe cancellation hooks.

## Known Limitations
- Generative speed on the baseline CPU backend is very slow. Vectorization is scheduled for future work.
- Streaming and CUDA extension points have been prepared but are currently unmapped.
- Only Greedy Sampling is active at this stage.

## Migration Notes
- External embedders must now manually initialize their target tokenizer and adapter prior to instantiating the runtime session.
