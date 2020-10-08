#pragma once

#include <clean-core/hash.hh>

#include <phantasm-hardware-interface/arguments.hh>

namespace phi::hash
{
inline cc::hash_t compute(arg::shader_arg_shape const& v) { return cc::make_hash(v.num_srvs, v.num_uavs, v.num_samplers, v.has_cbv); }

inline cc::hash_t compute(arg::shader_arg_shapes const v)
{
    cc::hash_t res = 0;
    for (auto const& e : v)
        res = cc::hash_combine(res, compute(e));
    return res;
}
}
