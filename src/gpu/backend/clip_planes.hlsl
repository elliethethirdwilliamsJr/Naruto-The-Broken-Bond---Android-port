// Geometry-stage implementation of the six user clip planes. Distances are
// evaluated by the original vertex shader before its half-pixel adjustment,
// then carried as user varyings. No native ClipDistance capability is used.
#include "clip_interface.hlsli"

[maxvertexcount(9)]
void main(triangle ClipVertex input[3], inout TriangleStream<RasterVertex> stream) {
    bool inside = true;
    [unroll] for (uint vertex = 0; vertex < 3; ++vertex)
        inside = inside && !any(input[vertex].clip0 < 0) && !any(input[vertex].clip1 < 0);
    if (inside) {
        stream.Append(Raster(input[0]));
        stream.Append(Raster(input[1]));
        stream.Append(Raster(input[2]));
        stream.RestartStrip();
        return;
    }

    // A triangle clipped by six half spaces is a convex polygon with at most
    // nine vertices. Keep positions, all interpolators and remaining distances
    // at each intersection; rasterization performs perspective interpolation.
    ClipVertex polygon[9];
    [unroll] for (uint vertex = 0; vertex < 3; ++vertex) polygon[vertex] = input[vertex];
    uint count = 3;
    [loop] for (uint plane = 0; plane < 6; ++plane) {
        ClipVertex next[9];
        uint next_count = 0;
        ClipVertex previous = polygon[count - 1];
        float previous_distance = Distance(previous, plane);
        bool previous_inside = previous_distance >= 0;
        [loop] for (uint vertex = 0; vertex < count; ++vertex) {
            ClipVertex current = polygon[vertex];
            float current_distance = Distance(current, plane);
            bool current_inside = current_distance >= 0;
            if (previous_inside != current_inside)
                next[next_count++] = Intersect(previous, current,
                    previous_distance / (previous_distance - current_distance));
            if (current_inside) next[next_count++] = current;
            previous = current;
            previous_distance = current_distance;
            previous_inside = current_inside;
        }
        if (next_count < 3) return;
        count = next_count;
        [loop] for (uint vertex = 0; vertex < count; ++vertex) polygon[vertex] = next[vertex];
    }
    // Alternating from each end triangulates a convex polygon as one strip.
    // This keeps winding and needs nine output vertices rather than a 21-vertex
    // triangle fan, reducing the geometry-output limit required of the device.
    [loop] for (uint vertex = 0; vertex < count; ++vertex) {
        uint index = vertex == 0 ? 0 : (vertex & 1) ? (vertex + 1) / 2 : count - vertex / 2;
        stream.Append(Raster(polygon[index]));
    }
    stream.RestartStrip();
}
