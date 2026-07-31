# Runtime API Reference

The public interface for Hummingbird's runtime engine.

## Types

`hbi_runtime_session`: Opaque pointer representing a generation session.
`hbi_runtime_config`: Configuration for inference (max tokens, sampler type, eos token).

## Functions

### `hbi_runtime_session_create`
```c
hbi_status hbi_runtime_session_create(const hbi_load_session *model, const hbi_model_adapter *adapter, const hbi_tokenizer *tokenizer, const hbi_runtime_config *config, hbi_allocator *allocator, hbi_runtime_session **out);
```
Creates a new runtime session.

### `hbi_runtime_session_destroy`
```c
void hbi_runtime_session_destroy(hbi_runtime_session *s);
```
Destroys the session and cleans up memory.

### `hbi_runtime_generate`
```c
hbi_status hbi_runtime_generate(hbi_runtime_session *s, const char *prompt, void (*on_text)(const char *text, void *ud), void *user_data);
```
Starts the generation loop. Callbacks are executed synchronously.

### `hbi_session_cancel`
```c
hbi_status hbi_session_cancel(hbi_runtime_session *s);
```
Safely flags the runtime loop to abort at the next token boundary.
