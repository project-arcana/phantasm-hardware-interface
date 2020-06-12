#include "unique_buffer.hh"

#include <fstream>

phi::detail::unique_buffer phi::detail::unique_buffer::create_from_binary_file(const char* filename)
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

bool phi::detail::unique_buffer::write_to_binary_file(const char* filename)
{
    std::ofstream file(filename, std::ios::binary);
    if (!file.good())
        return false;

    file.write(get_as_char(), size());
    file.close();
    return !file.fail();
}
