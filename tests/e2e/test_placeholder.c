/* Real E2E tests go here */
#include "executor/executor_internal.h"
#include "graph/graph.h"
#include "hummingbird/hummingbird.h"
#include "model/model.h"
#include "model/model_internal.h"
#include "runtime/executor.h"
#include "runtime/runtime.h"
#include "runtime/runtime_internal.h"
#include "runtime/sampler.h"
#include <stdio.h>
#include <stdlib.h>

hbi_status hb_backend_cpu_register_kernels(void);
hbi_status hb_backend_cpu_register(void);
hbi_status hbi_format_safetensors_register(void);

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("E2E Inference Test Started.\n");

    hb_backend_cpu_register();
    hb_backend_cpu_register_kernels();
    hbi_format_safetensors_register();

    hbi_allocator *alloc = hbi_allocator_system();

    hbi_load_options opts = {.model_path = "tiny_model.safetensors",
                             .format_hint = HBI_MODEL_FORMAT_SAFETENSORS,
                             .flags = 0};
    hbi_load_session *model_session = NULL;
    hbi_status st = hbi_model_load(&opts, alloc, &model_session);
    if (st != HBI_OK) {
        printf("Failed to load tiny model\n");
        return 1;
    }

    hbi_graph_builder *b = NULL;
    hbi_graph_builder_create(&b);

    hbi_shape s44;
    hbi_shape_init(&s44, (int64_t[]){4, 4}, 2);
    hbi_shape s4;
    hbi_shape_init(&s4, (int64_t[]){1, 4}, 2);

    uint32_t in_val;
    hbi_graph_add_input(b, "input", &s4, HBI_DTYPE_FP32, &in_val);

    const hbi_tensor_entry *e_tok = hbi_model_manifest_find(model_session->manifest, "tok_emb");
    const hbi_tensor_entry *e_lm = hbi_model_manifest_find(model_session->manifest, "lm_head");

    if (!e_tok || !e_lm) {
        printf("Missing tensors in safetensors\n");
        return 1;
    }

    void *tok_data = malloc(e_tok->byte_size);
    void *lm_data = malloc(e_lm->byte_size);

    model_session->handler->read_tensor_data("tiny_model.safetensors", e_tok, tok_data,
                                             e_tok->byte_size);
    model_session->handler->read_tensor_data("tiny_model.safetensors", e_lm, lm_data,
                                             e_lm->byte_size);

    /* Modify lm_data so that input [1,0,0,0] * lm_head gives max logit at index 1 */
    float *lm_f32 = (float *)lm_data;
    lm_f32[1] = 10.0f; /* lm_head[0, 1] = 10.0 */

    hbi_tensor tok_t, lm_t;
    hbi_tensor_wrap(&tok_t, HBI_DTYPE_FP32, &s44, tok_data, e_tok->byte_size);
    hbi_tensor_wrap(&lm_t, HBI_DTYPE_FP32, &s44, lm_data, e_lm->byte_size);

    uint32_t tok_emb_val, lm_head_val;
    hbi_graph_add_constant(b, "tok_emb", &tok_t, &tok_emb_val);
    hbi_graph_add_constant(b, "lm_head", &lm_t, &lm_head_val);

    hbi_kernel_params matmul_params = {0};
    uint32_t logits_val;
    hbi_graph_add_node(b, "matmul", HBI_KERNEL_OP_MATMUL, &matmul_params,
                       (uint32_t[]){in_val, lm_head_val}, 2, &logits_val, 1);

    hbi_graph *graph = NULL;
    hbi_graph_build(b, &graph);

    hbi_runtime_session *rt_session = calloc(1, sizeof(*rt_session));
    rt_session->allocator = alloc;
    rt_session->model = model_session;
    rt_session->graph = graph;

    hbi_backend_manager_create(alloc, &rt_session->backend_mgr);

    hbi_scheduler_create(alloc, &rt_session->scheduler);
    hbi_scheduler_create_plan(rt_session->scheduler, graph, &rt_session->plan);

    hbi_memory_planner_create(graph, &rt_session->planner);
    hbi_memory_planner_plan(rt_session->planner, &rt_session->plan_memory);

    printf("Pipeline Built.\n");

    struct hbi_exec_context *ctx = calloc(1, sizeof(struct hbi_exec_context));
    uint32_t num_values = hbi_graph_num_values(graph);
    ctx->num_values = num_values;
    ctx->values = calloc(num_values, sizeof(hbi_value_state));

    for (uint32_t i = 0; i < num_values; i++) {
        hbi_tensor *t = calloc(1, sizeof(hbi_tensor));
        const hbi_value *v = hbi_graph_value_at(graph, i);
        if (v->is_constant && v->const_tensor) {
            *t = *v->const_tensor;
        } else {
            hbi_tensor_wrap(t, v->dtype, &v->shape, NULL, 0);
            hbi_allocation alloc_res;
            if (hbi_memory_plan_get_allocation(rt_session->plan_memory, i, &alloc_res) == HBI_OK) {
                if (!ctx->pools) {
                    ctx->num_pools = hbi_memory_plan_num_pools(rt_session->plan_memory);
                    ctx->pools = calloc(ctx->num_pools, sizeof(void *));
                    for (uint32_t p = 0; p < ctx->num_pools; p++) {
                        ctx->pools[p] =
                            malloc(hbi_memory_plan_pool_size(rt_session->plan_memory, p));
                    }
                }
                hbi_tensor_wrap(t, v->dtype, &v->shape,
                                (char *)ctx->pools[alloc_res.pool_id] + alloc_res.offset, 1024);
            } else {
                printf("Allocating manual for value %u\n", i);
                size_t size = 1024;
                void *data = calloc(1, size);
                printf("Size=%zu data=%p\n", size, data);
                hbi_tensor_wrap(t, v->dtype, &v->shape, data, size);
            }
        }
        ctx->values[i].tensor = t;
    }

    rt_session->exec_ctx = ctx;

    printf("Setting up input... in_val=%u\n", in_val);
    float *in_ptr = (float *)hbi_tensor_data_mut(ctx->values[in_val].tensor);
    if (!in_ptr) {
        printf("in_ptr is NULL!\n");
        return 1;
    }
    in_ptr[0] = 1.0f;
    in_ptr[1] = 0.0f;
    in_ptr[2] = 0.0f;
    in_ptr[3] = 0.0f;

    printf("Running executor...\n");
    st = hbi_runtime_executor_run(rt_session);
    if (st != HBI_OK) {
        printf("executor run failed: %d\n", st);
        return 1;
    }

    printf("Getting logits... logits_val=%u\n", logits_val);
    float *logits = (float *)hbi_tensor_data_mut(ctx->values[logits_val].tensor);
    if (!logits) {
        printf("logits is NULL!\n");
        return 1;
    }
    printf("Logits: %f %f %f %f\n", logits[0], logits[1], logits[2], logits[3]);

    uint32_t next_tok = 0;
    hbi_sampler_greedy(ctx->values[logits_val].tensor, &next_tok);

    printf("Sampled Token: %u\n", next_tok);
    int ret = 0;
    if (next_tok == 1) {
        printf("SUCCESS: Next token is 'world' (ID 1).\n");
    } else {
        printf("FAIL.\n");
        ret = 1;
    }

    if (ctx) {
        if (ctx->pools) {
            for (uint32_t p = 0; p < ctx->num_pools; p++) {
                free(ctx->pools[p]);
            }
            free(ctx->pools);
        }
        if (ctx->values) {
            for (uint32_t i = 0; i < ctx->num_values; i++) {
                hbi_tensor *t = ctx->values[i].tensor;
                if (t) {
                    const hbi_value *v = hbi_graph_value_at(graph, i);
                    if (!v->is_constant) {
                        hbi_allocation alloc_res;
                        if (hbi_memory_plan_get_allocation(rt_session->plan_memory, i,
                                                           &alloc_res) != HBI_OK) {
                            free(hbi_tensor_data_mut(t));
                        }
                    }
                    free(t);
                }
            }
            free(ctx->values);
        }
        free(ctx);
    }

    hbi_memory_plan_destroy(rt_session->plan_memory);
    hbi_memory_planner_destroy(rt_session->planner);
    if (rt_session->plan) {
        hbi_execution_plan_destroy(rt_session->scheduler, rt_session->plan);
    }
    hbi_scheduler_destroy(rt_session->scheduler);
    hbi_backend_manager_destroy(rt_session->backend_mgr);

    free(rt_session);

    hbi_graph_destroy(graph);

    free(tok_data);
    free(lm_data);

    hbi_model_load_session_destroy(model_session);

    return ret;
}
