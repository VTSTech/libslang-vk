// libslang-vk runner regression canary (field round 6, crt-consumer):
// a plain UBO whose member NAME starts with a qualifier prefix.
// ParseBlockMembers strips "flat"/"smooth"/"noperspective"/precision
// qualifiers from member declarations; the old left-boundary-only check
// erased "smooth" out of "float smoothness;" and parsed the bogus member
// "ness". The uniform fill then found no parameter for "ness", left the
// member at 0, and crt-consumer's corner mask (border-smoothness driven)
// multiplied every pixel to black.
//
// The fix requires a token boundary on BOTH sides of the qualifier. This
// canary pins the shapes (interpolation qualifiers are illegal on block
// members, so the real-qualifier case uses a precision qualifier):
//   float smoothness;        - qualifier PREFIX inside a name (was broken)
//   float highprecision;     - same bug, different qualifier (was broken)
//   highp float flatline;    - real precision qualifier (still works)
// Encoded golden at the pragma defaults: R = smoothness*0.5-199 = 1,
// G = highprecision-1 = 3, B = flatline-1 = 2 (byte-exact in fp32 + UNORM).
#pragma name slvtest_smoothqual

#pragma parameter smoothness "Border Smoothness" 400.0 100.0 600.0 5.0
#pragma parameter highprecision "High Precision" 4.0 0.0 8.0 1.0
#pragma parameter flatline "Flat Line" 3.0 0.0 8.0 1.0

layout(std140, set = 0, binding = 0) uniform UBO
{
        mat4 MVP;
        float smoothness;
        float highprecision;
        highp float flatline;
} global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main() {
        gl_Position = global.MVP * Position;
        vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

void main() {
        // Read every member so SPIR-V dead-code elimination cannot strip the
        // block (an unused member hides the fill entirely).
        vec3 src = texture(Source, vTexCoord).rgb;
        FragColor = vec4((global.smoothness * 0.5 - 199.0) / 255.0, (global.highprecision - 1.0) / 255.0,
            (global.flatline - 1.0) / 255.0 + src.b * 0.0, 1.0);
}
