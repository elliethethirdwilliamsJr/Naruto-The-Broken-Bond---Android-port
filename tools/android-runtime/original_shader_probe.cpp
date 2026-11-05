// Isolate the original Flash/menu shader pair observed in the black frame.
#include "gpu/backend/engine.h"
#include "gpu/shaders/package.h"
#include "solid_pixel.h"
#include <rex/logging.h>
#include <cstring>
#include <fstream>
#include <stdexcept>

using namespace gpu;
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) return 2;
  rex::InitLoggingEarly();
  std::ofstream out(argv[1]); out.setf(std::ios::unitbuf);
  Engine engine; std::string evidence; RenderMode mode; mode.api = GraphicsApi::kVulkan;
  if (!engine.Initialize(evidence, mode)) { out << evidence << '\n'; return 3; }
  out << "{\"phase\":\"initialize\",\"evidence\":" << evidence << "}\n";
  const auto* package = EmbeddedShaderPackage(ShaderFormat::kSpirv, evidence);
  if (!package) return 4;
  auto shader = [&](const char* name, ShaderStage stage) {
    if (stage == ShaderStage::kVertex && argc == 3) {
      std::ifstream input(argv[2], std::ios::binary);
      std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
      if (bytes.empty()) throw std::runtime_error("Missing diagnostic vertex module");
      return ShaderRef{name, stage, ShaderFormat::kSpirv,
          std::make_shared<const std::vector<uint8_t>>(std::move(bytes))};
    }
    return ShaderRef{name, stage, ShaderFormat::kSpirv,
        std::make_shared<const std::vector<uint8_t>>(DecodeShaderModule(*package, package->modules.at(name)))};
  };
  if (!engine.CreateSurface({0xD001, 256, 256}, evidence)) return 5;
  DrawRequest draw;
  draw.elements = std::make_shared<const std::vector<VertexElement>>(std::vector<VertexElement>{
      {"POSITION", 0, VertexFormat::kR32Uint, 0, 0},
      {"COLOR", 0, VertexFormat::kR32Uint, 0, 4},
      {"COLOR", 1, VertexFormat::kR32Uint, 0, 8}});
  const auto packed = [](int x, int y) { return uint32_t(uint16_t(x)) | (uint32_t(uint16_t(y)) << 16); };
  const uint32_t vertices[]{packed(-10000,-10000), 0, 0xffffffff, packed(10000,-10000), 0, 0xffffffff,
                            packed(-10000,10000), 0, 0xffffffff, packed(10000,10000), 0, 0xffffffff};
  draw.streams.push_back({std::vector<uint8_t>(sizeof(vertices)), 12});
  std::memcpy(draw.streams[0].data.data(), vertices, sizeof(vertices));
  draw.indices = {0,0,1,0,2,0}; draw.index_count = 3;
  draw.color_key = 0xD001; draw.viewport = {0,0,256,256,0,1}; draw.scissor = {0,0,256,256};
  draw.vs_constants.resize(4096); draw.ps_constants.resize(4096);
  auto vector = [](auto& block, size_t at, std::array<float,4> value) {
    std::memcpy(block.data() + at * 16, value.data(), 16);
  };
  vector(draw.vs_constants,0,{3.6733007e-5f,0,0,0});
  vector(draw.vs_constants,1,{0,-6.529516e-5f,0,0});
  vector(draw.vs_constants,2,{0,0,-1,1}); vector(draw.vs_constants,3,{0,0,0,1});
  vector(draw.vs_constants,42,{1,1,1,1});
  vector(draw.vs_constants,44,{1.0113283e-5f,0,0,0.5020864f});
  vector(draw.vs_constants,45,{0,1.3951988e-5f,0,0.50207716f});
  vector(draw.ps_constants,12,{0.5019608f,0.5019608f,0.5019608f,1});
  const uint32_t fetch[]{1442521,791558,791558};
  std::memcpy(draw.shared_constants.data()+320, fetch, sizeof(fetch));
  DrawTexture texture; texture.reg=0; texture.key=0xD002; texture.width=texture.height=1;
  texture.levels=1; texture.layout=texture.version=1; texture.format=TextureFormat::kR8G8B8A8Unorm;
  texture.data={{255,255,255,255}};
  if (!engine.UploadSampled(texture, evidence)) return 6;
  texture.data.clear(); draw.textures={texture};
  std::vector<uint8_t> baseline[2];
  bool passed = true; unsigned comparisons = 0;
  for (unsigned topology=0;topology<2;++topology) {
   draw.topology = topology ? Topology::kTriangleStrip : Topology::kTriangleList;
   draw.indices = topology ? std::vector<uint8_t>{0,0,1,0,2,0,3,0} : std::vector<uint8_t>{0,0,1,0,2,0,2,0,1,0,3,0};
   draw.index_count = topology ? 4 : 6;
   for (unsigned variant=0;variant<8;++variant) {
    const bool geometry=variant&1, solid=variant&2, clipped=variant&4;
    const float plane[]{clipped ? 1.0f : 0.0f,0,0,0};
    std::memcpy(draw.shared_constants.data()+384,plane,sizeof(plane));
    draw.vs=shader("vs-000C5F28",ShaderStage::kVertex);
    if (!geometry) draw.vs.key="diagnostic-original-vertex";
    draw.ps=solid ? ShaderRef{"solid-pixel",ShaderStage::kPixel,ShaderFormat::kSpirv,
        std::make_shared<const std::vector<uint8_t>>(reinterpret_cast<const uint8_t*>(kSolidPs),
        reinterpret_cast<const uint8_t*>(kSolidPs)+sizeof(kSolidPs))} : shader("ps-002D88B0",ShaderStage::kPixel);
    ClearRequest clear; clear.color_keys[0]=draw.color_key; clear.clear_color=true;
    uint64_t fence{};
    const bool drawn=engine.ClearSurfaces(clear,fence,evidence) && engine.DrawIndexed(draw,fence,evidence);
    std::vector<uint8_t> pixels; uint32_t width{},height{};
    const bool read=drawn && engine.ReadSurface(draw.color_key,fence,pixels,width,height,evidence);
    size_t nonblack=0;
    for(size_t at=0;at+4<=pixels.size();at+=4) nonblack+=(pixels[at]|pixels[at+1]|pixels[at+2])!=0;
    if (read && !topology && !geometry && !clipped) baseline[solid ? 1 : 0]=pixels;
    auto expected = baseline[solid ? 1 : 0];
    if (geometry && clipped) for (uint32_t y=0;y<height;++y)
      std::memset(expected.data()+size_t(y)*width*4,0,size_t(width/2)*4);
    const bool correct=read && nonblack>0 && pixels==expected;
    passed &= correct; ++comparisons;
    out << "{\"topology\":" << topology << ",\"clipped\":" << (clipped?"true":"false")
        << ",\"exact_pixels\":" << (correct?"true":"false") << ",\"geometry\":" << (geometry?"true":"false") << ",\"solid_pixel\":" << (solid?"true":"false")
        << ",\"read\":" << (read?"true":"false") << ",\"nonblack_pixels\":" << nonblack
        << ",\"evidence\":" << (evidence.empty()?"null":evidence) << "}\n";
    if(!read) return 7;
  }
  }
  const auto summary = engine.PipelineSummary();
  const auto at = summary.find("\"created\":");
  const uint64_t created = at == std::string::npos ? 0 : std::strtoull(summary.c_str()+at+10,nullptr,10);
  passed &= created == 8;  // Four shader pairs for each independently keyed topology.
  engine.ReleaseRemaining(evidence); engine.Shutdown(evidence);
  out << "{\"passed\":" << (passed?"true":"false") << ",\"comparisons\":" << comparisons
      << ",\"pipelines_created\":" << created << "}\n";
  return passed ? 0 : 1;
}
