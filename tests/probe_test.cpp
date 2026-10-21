#include <cstdio>
#include <optional>
#include <slangvk/slangvk.hpp>
int main(int argc, char **argv)
{
        const bool releaseChainFirst = (argc > 1);
        slv::DeviceOptions o;
        o.appName = "probe";
        auto device = slv::Device::create(o);
        if (!device) { std::printf("no device\n"); return 0; }
        auto preset = slv::Preset::load(*device, std::string(SLANGVK_TESTS_DATA_DIR) + "/runner/blit.slangp");
        if (!preset) { std::printf("no preset\n"); return 0; }
        slv::ChainOptions co{64, 64, VK_FORMAT_R8G8B8A8_UNORM};
        {
                auto chain = slv::Chain::create(*device, *preset, co);
                if (!chain) { std::printf("no chain\n"); return 0; }
                if (releaseChainFirst) {
                        chain = std::unexpected(slv::Error{});
                        std::printf("chain released before the device replacement\n");
                } else {
                        std::printf("chain still alive across the device replacement\n");
                }
                auto r = device->recover();
                std::printf("recover: %s\n", r ? "ok" : r.error().message.c_str());
        }
        return 0;
}
