"""Compare an Android `cmd gpu vkjson` capture with the current renderer.

This is a read-only preflight, not a game or driver validation. Exit 2 means a
confirmed blocker, 1 means the capture/source could not be assessed, and 0 means
only that these checks found no blocker. Android integration is still required.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


def assess(capture, root, device_index, shader_manifest=None):
    devices = json.loads(capture.read_text(encoding="utf-8-sig"))["devices"]
    device = devices[device_index]
    core = device["features"]
    core12 = device["core12"]["features"]
    properties = device["properties"]
    version = int(properties["apiVersion"])
    formats = {int(key): value for key, value in device["formats"]}

    # Use this checkout's format mappings and Vulkan enum values, rather than a
    # second hand-maintained texture table. Refuse an incomplete dependency tree.
    backend = root / "src/gpu/backend/vulkan"
    mappings = (backend / "formats.h").read_text(encoding="utf-8")
    header = (root / "build/deps/plume/contrib/Vulkan-Headers/include/vulkan/vulkan_core.h").read_text(encoding="utf-8")
    contract = (backend / "shader_contract.cpp").read_text(encoding="utf-8")
    startup = (backend / "backend.cpp").read_text(encoding="utf-8")
    if not all(marker in contract for marker in ("features.shaderClipDistance", "enabled.descriptorIndexing")):
        raise ValueError("Shader requirements changed; review the preflight mapping.")
    if "for (TextureFormat format : kTextureFormats)" not in startup:
        raise ValueError("Texture startup requirements changed; review the preflight mapping.")
    if "SelectSampledFormat(format, sampled_supported)" not in startup or "DecodeSampledData(t, host_format" not in startup:
        raise ValueError("Texture upload fallback integration changed; review the preflight mapping.")
    host_formats = dict(re.findall(r"case TextureFormat::(k\w+): return (VK_FORMAT_\w+);", mappings))
    decoder = (root / "src/gpu/textures/formats.cpp").read_text(encoding="utf-8")
    fallback_body = re.search(r"TextureFormat DecodedSampledFormat\([^)]*\)\s*\{(.*?)\n\}", decoder, re.S)
    if not fallback_body:
        raise ValueError("Sampled texture decoder format mapping is absent.")
    fallbacks = {}
    for match in re.finditer(r"((?:\s*case TextureFormat::k\w+:\s*)+)return TextureFormat::(k\w+);", fallback_body[1]):
        for source in re.findall(r"case TextureFormat::(k\w+):", match[1]):
            fallbacks[host_formats[source]] = host_formats[match[2]]
    if len(fallbacks) != 4:
        raise ValueError("BC fallback format table changed; review the preflight mapping.")

    def enum(name):
        match = re.search(r"\b" + re.escape(name) + r"\s*=\s*(0x[\da-fA-F]+|\d+)\s*,", header)
        if not match:
            raise ValueError(f"Vulkan enum is absent: {name}")
        return int(match[1], 0)

    sampled = enum("VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT")
    src = enum("VK_FORMAT_FEATURE_TRANSFER_SRC_BIT")
    dst = enum("VK_FORMAT_FEATURE_TRANSFER_DST_BIT")
    required = sampled | src | dst
    rows = []
    names = set(re.findall(r"return (VK_FORMAT_\w+);", mappings)) - {"VK_FORMAT_UNDEFINED"}
    if not names:
        raise ValueError("No texture format mappings found.")
    for name in sorted(names):
        value = enum(name)
        flags = int(formats.get(value, {}).get("optimalTilingFeatures", 0))
        rows.append(dict(format=name, value=value, optimal_tiling_features=flags,
                         sampled=bool(flags & sampled), transfer_src=bool(flags & src),
                         transfer_dst=bool(flags & dst),
                         supported=(flags & required == required and
                                    (not name.startswith("VK_FORMAT_BC") or bool(core["textureCompressionBC"])))))
    native_rows = {row["format"]: row for row in rows}
    cpu_fallbacks = []
    texture_upload_blockers = []
    for row in rows:
        fallback = fallbacks.get(row["format"])
        fallback_supported = bool(fallback and native_rows[fallback]["supported"])
        row["selected_upload_format"] = row["format"] if row["supported"] else fallback if fallback_supported else None
        row["cpu_decoded"] = not row["supported"] and fallback_supported
        row["upload_supported"] = row["selected_upload_format"] is not None
        if row["cpu_decoded"]:
            cpu_fallbacks.append(dict(source=row["format"], destination=fallback))
        if not row["upload_supported"]:
            texture_upload_blockers.append(row["format"])

    features = {
        "shader_clip_distance": bool(core["shaderClipDistance"]),
        "geometry_shader": bool(core["geometryShader"]),
        "descriptor_indexing": bool(core12["descriptorIndexing"]),
        "runtime_descriptor_array": bool(core12["runtimeDescriptorArray"]),
        "sampled_image_nonuniform_indexing": bool(core12["shaderSampledImageArrayNonUniformIndexing"]),
        "sampled_image_array_dynamic_indexing": bool(core["shaderSampledImageArrayDynamicIndexing"]),
        "buffer_device_address": bool(core12["bufferDeviceAddress"]),
        "shader_int64": bool(core["shaderInt64"]),
        "storage_image_extended_formats": bool(core["shaderStorageImageExtendedFormats"]),
        "texture_compression_bc": bool(core["textureCompressionBC"]),
    }
    unsupported = [row["format"] for row in rows if not row["supported"]]
    required_features = ("shader_clip_distance", "descriptor_indexing", "runtime_descriptor_array",
                         "buffer_device_address", "shader_int64")
    blockers = ["feature:" + name for name in required_features if not features[name]]
    compiled_contract = None
    if shader_manifest:
        manifest = json.loads(shader_manifest.read_text(encoding="utf-8-sig"))
        if manifest["schema"] != "native-shader-manifest-v2":
            raise ValueError("Unsupported shader manifest schema.")
        contract = manifest["spirv"]
        digest = hashlib.sha256(json.dumps(contract, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        if digest != manifest["contract_sha256"]:
            raise ValueError("Shader contract digest does not match its manifest.")
        available = {
            "Shader": True, "ImageQuery": True, "DerivativeControl": True,
            "ClipDistance": features["shader_clip_distance"],
            "Geometry": features["geometry_shader"],
            "Int64": features["shader_int64"],
            "StorageImageExtendedFormats": features["storage_image_extended_formats"],
            "PhysicalStorageBufferAddresses": features["buffer_device_address"],
            "RuntimeDescriptorArray": features["descriptor_indexing"] and features["runtime_descriptor_array"],
            "SampledImageArrayDynamicIndexing": features["sampled_image_array_dynamic_indexing"],
        }
        needed = contract["required"]
        missing_caps = [name for name in needed["capabilities"] if not available.get(name, False)]
        missing_exts = [name for name in needed["extensions"]
                        if name != "SPV_EXT_descriptor_indexing" or not features["descriptor_indexing"]]
        blockers = ["capability:" + name for name in missing_caps] + ["extension:" + name for name in missing_exts]
        compiled_contract = dict(sha256=digest, required=needed, missing_capabilities=missing_caps,
                                 missing_extensions=missing_exts,
                                 unmapped_capabilities=[name for name in needed["capabilities"] if name not in available])
        if contract['stage_interface'].get('clip_mode') == 'geometry':
            geometry_limits = contract['stage_interface'].get('geometry_limits')
            if not geometry_limits:
                raise ValueError('Geometry clipping contract lacks its interface limits.')
            for name, minimum in geometry_limits.items():
                if int(properties['limits'].get(name, 0)) < minimum:
                    blockers.append('geometry-limit:' + name)
            compiled_contract['geometry_limits'] = geometry_limits
            compiled_contract['geometry_observed_limits'] = {
                name: int(properties['limits'].get(name, 0)) for name in geometry_limits}
        if contract['layout'].get('descriptor_mode') == 'bounded':
            descriptor_limits = contract['layout'].get('descriptor_limits')
            if not descriptor_limits or contract['layout'].get('descriptor_slots') != 64:
                raise ValueError('Bounded descriptor contract lacks its slot/limit mapping.')
            if not features['sampled_image_array_dynamic_indexing']:
                blockers.append('feature:sampled_image_array_dynamic_indexing')
            for name, minimum in descriptor_limits.items():
                if int(properties['limits'].get(name, 0)) < minimum:
                    blockers.append('descriptor-limit:' + name)
            compiled_contract['descriptor_limits'] = descriptor_limits
    blockers += ["format:" + name for name in texture_upload_blockers]
    limit_names = ("maxBoundDescriptorSets", "maxPerStageDescriptorSampledImages",
                   "maxPerStageDescriptorSamplers", "maxVertexOutputComponents",
                   "maxFragmentInputComponents", "maxVertexInputAttributes",
                   "maxUniformBufferRange", "maxPushConstantsSize")
    limits = {name: int(properties["limits"][name]) for name in limit_names}
    return dict(device=properties["deviceName"],
                vulkan=f"{version >> 22}.{version >> 12 & 1023}.{version & 4095}",
                **features, resource_limits=limits, compiled_shader_contract=compiled_contract,
                texture_format_requirements=rows,
                cpu_texture_fallbacks=cpu_fallbacks, texture_upload_blockers=texture_upload_blockers,
                unsupported_texture_formats=unsupported, blockers=blockers,
                assessment=("Current native Vulkan renderer cannot run unchanged on this device."
                            if blockers else "No blocker in the checked subset; full shader-contract and runtime validation remain required."),
                scope="Read-only physical-device capture and source comparison; no game or APK run. Not an exhaustive compatibility certification.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--device-index", type=int, default=0)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--shader-manifest", type=Path, help="also compare the successfully compiled shader contract")
    args = parser.parse_args()
    try:
        if args.device_index < 0:
            raise ValueError("Device index must be nonnegative.")
        if args.output and args.output.resolve() == args.capture.resolve():
            raise ValueError("The output must differ from the input capture.")
        report = assess(args.capture, Path(__file__).resolve().parents[1], args.device_index, args.shader_manifest)
        rendered = json.dumps(report, indent=2) + "\n"
        if args.output:
            args.output.write_text(rendered, encoding="utf-8")
        print(rendered, end="")
        return 2 if report["blockers"] else 0
    except (OSError, KeyError, IndexError, TypeError, ValueError) as error:
        print(f"Cannot assess Vulkan compatibility: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
