import json
import struct

vocab = {"Hello": 0, "world": 1, "!": 2, "[EOS]": 3}
inv_vocab = {v: k for k, v in vocab.items()}

# Tiny model: vocab_size=4, hidden_size=4
# Weights:
# tok_emb: [4, 4]
# output_norm: [4]
# lm_head: [4, 4] (let's just use token embeddings tied)

# Let's write a simple safetensors file
tensor_info = {
    "tok_emb": {"dtype": "F32", "shape": [4, 4], "data_offsets": [0, 64]},
    "lm_head": {"dtype": "F32", "shape": [4, 4], "data_offsets": [64, 128]},
    "__metadata__": {"format": "pt", "model_type": "tiny"}
}

import numpy as np

tok_emb = np.eye(4, dtype=np.float32) 
lm_head = np.eye(4, dtype=np.float32)

# If input is 0 ("Hello"), tok_emb is [1,0,0,0]. 
# lm_head * [1,0,0,0] = [1,0,0,0]. Logits will favor 0 ("Hello"). 
# Let's make lm_head shift it: 0 -> 1, 1 -> 2, 2 -> 3
lm_head = np.zeros((4,4), dtype=np.float32)
lm_head[1, 0] = 10.0 # Hello -> world
lm_head[2, 1] = 10.0 # world -> !
lm_head[3, 2] = 10.0 # ! -> [EOS]
lm_head[3, 3] = 10.0 # EOS -> EOS

data = tok_emb.tobytes() + lm_head.tobytes()

header = json.dumps(tensor_info).encode("utf-8")
header_len = struct.pack("<Q", len(header))

with open("tiny_model.safetensors", "wb") as f:
    f.write(header_len)
    f.write(header)
    f.write(data)
    
print("tiny_model.safetensors created.")
