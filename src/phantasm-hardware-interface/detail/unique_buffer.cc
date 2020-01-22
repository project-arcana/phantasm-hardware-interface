#include "unique_buffer.hh"

#include <fstream>

pr::backend::detail::unique_buffer pr::backend::detail::unique_buffer::create_from_binary_file(const char* filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.good())
        return detail::unique_buffer{};
    auto const size = size_t(file.tellg());
    detail::unique_buffer res(size);
    file.seekg(0);
    file.read(res.get_as_char(), long(size));
    return res;
}
