// DynLandscape fragment stage — hand-authored GLSL port of dyn_landscape.arbfp1
// (!!ARBfp1.0).
//
// PROFILE-NEUTRAL BODY: no #version, no varying declarations, no precision
// statements and no uniform block declarations in this file.
// CDynLandscape::setupPrograms() prepends the per-profile prefix
// (glsl330 SSO or glsl300es linked) that declares the varyings and the
// 'fragColor' output, and the GL3 driver injects the builtin uniform blocks
// (NlCamera + NlLightTable via Features.UsesCameraUBO/UsesLightTableUBO) and
// the user "NlDynLandscape" params block (insertBuiltinHeaders) — none of
// those may be declared here.
//
// Consumed varyings : ecPos (NeL-space eye position), texCoord0 (scaled
//                     object position, triplanar source coordinates),
//                     texCoord1 (triplanar blend weights),
//                     texCoord2 ((object normal, w = per-vertex alpha)).
// Samplers          : sampler0 / sampler1 (CProgramIndex::Sampler0/1 names)
//                     so the driver auto-binds material stage textures 0/1.
// Consumed builtins : fogColor, fogParams (NlCamera); nlLights[0] entry the
//                     sun in both light-table modes (render_trav stages the
//                     sun as entry 0). User UBO member landscapeParams
//                     (vec4: x = tighten [unused here], y = scale [unused
//                     here], z = displayFactor, w = DEBUG MODE — see below).
//
// Semantics ported from the assembly:
//   triplanar fetch: texture 1 on the X plane (coord zy, weight TEX1.x),
//   Y plane (coord zx, weight TEX1.y), and Z plane (coord yx, weight
//   TEX1.z) where the plane selector picks texture 0 for normal.z > 0
//   (floor) and texture 1 otherwise (ceiling) — asm SLT/ABS/CMP chain.
//   lighting: nlLights[0] ambient + LIT-clamped diffuse dot product.
//   fog: MAD_SAT/LRP linear pair == builtin applyFog linear branch.
//   alpha: texCoord2.w * displayFactor (asm c[2].z).
//
// DEBUG MODES (landscapeParams.w, set in-game via 'dyn_shaderdebug <n>',
// staged by dyn_landscape.cpp g_shaderDebugMode): all debug modes pin
// alpha = 1 so blending cannot hide the tested component.
//   0 — production path (exactly the ported asm semantics).
//   1 — flat magenta: "does this pipeline draw anything at all?"
//   2 — unlit textured: raw triplanar color, no light, no fog.
//       "texture plumbing + VP TEX0/1/2 varyings", then test lighting...
//   3 — lighting only: green base + sun ambient/diffuse product, no
//       textures, no fog. "is nlLights[0] a usable sun?"
//   4 — fog only: grayscale fog factor, no textures, no light.
//       "is ecPos / fogParams math sane?"
//   5 — texcoords as color: fract(texCoord0.xyz). "is VP TEX0 sane?"
//   6 — production color, alpha forced to 1. "is the alpha chain
//       (texCoord2.w * displayFactor) black/zero in production?"
//   7 — triplanar weights as color: abs(texCoord1.rgb) (R = X-plane wall
//       weight, G = Y-plane wall weight, B = Z-plane floor weight).
//       Slopes must show B fading into R/G up a cliff; everywhere-blue
//       with no rotation = the weights never arrive (VP/interface bug),
//       sane rotation but broken production blend = FP blend math bug.
//   8 — object normal as color: abs(texCoord2.rgb). Flat ground must be
//       green-dominant (0,1,0); cliffs red/blue-dominant. Paints the
//       delivered normal varying (the weights' source data).

void main()
{
	int dbg = int(landscapeParams.w + 0.5);

	if (dbg == 1)
	{
		fragColor = vec4(1.0, 0.0, 1.0, 1.0);
		return;
	}

	if (dbg == 2)
	{
		vec4 raw = texture(sampler1, vec2(texCoord0.z, texCoord0.y)) * texCoord1.x;
		raw += texture(sampler1, vec2(texCoord0.z, texCoord0.x)) * texCoord1.y;
		vec4 zRaw = (texCoord2.z > 0.0)
			? texture(sampler0, vec2(texCoord0.y, texCoord0.x))
			: texture(sampler1, vec2(texCoord0.y, texCoord0.x));
		raw += zRaw * texCoord1.z;
		fragColor = vec4(raw.rgb, raw.a);
		return;
	}

	if (dbg == 3)
	{
		vec3 n1 = normalize(texCoord2.xyz);
		float ndlDbg = clamp(dot(n1, -nlLights[0].dirOrPos), 0.0, 1.0);
		vec3 litDbg = nlLights[0].ambient.rgb + nlLights[0].diffuse.rgb * ndlDbg;
		// green base so a fully-black light contribution stays visible as dark green
		fragColor = vec4(vec3(0.0, 0.25, 0.0) + clamp(litDbg, 0.0, 1.0), 1.0);
		return;
	}

	if (dbg == 4)
	{
		float depthDbg = abs(ecPos.y / ecPos.w);
		float fogFactorDbg = clamp((fogParams.t - depthDbg) / (fogParams.t - fogParams.s), 0.0, 1.0);
		fragColor = vec4(vec3(fogFactorDbg), 1.0);
		return;
	}

	if (dbg == 5)
	{
		fragColor = vec4(fract(texCoord0.xyz), 1.0);
		return;
	}

	if (dbg == 7)
	{
		// Triplanar weights as color (same varying the fetches multiply by).
		fragColor = vec4(abs(texCoord1.rgb), 1.0);
		return;
	}

	if (dbg == 8)
	{
		// Object normal as color, straight from the texCoord2 varying.
		fragColor = vec4(abs(texCoord2.xyz), 1.0);
		return;
	}

	// Production path (modes 0 and 6).
	// Normalized normal. The asm transforms with the inverse transpose
	// (state.matrix.modelview.invtrans) into eye space and normalizes;
	// region matrices are unrotated and uniformly scaled, so the object-space
	// direction is already the valid world-space light basis here —
	// normalize the varying directly (documented Phase-3 parity note).
	vec3 n = normalize(texCoord2.xyz);

	// Triplanar diffuse fetch, asm coordinate swizzles replicated exactly.
	// X plane: texture 1, coord (z, y), weight texCoord1.x (asm: MUL R2)
	vec4 lit = texture(sampler1, vec2(texCoord0.z, texCoord0.y)) * texCoord1.x;
	// Y plane: texture 1, coord (z, x), weight texCoord1.y (asm: MAD)
	lit += texture(sampler1, vec2(texCoord0.z, texCoord0.x)) * texCoord1.y;
	// Z plane: coord (y, x), weight texCoord1.z; asm picks texture 0 on the
	// floor (normal.z > 0) and texture 1 on the ceiling via SLT/ABS/CMP.
	vec4 zTex = (texCoord2.z > 0.0)
		? texture(sampler0, vec2(texCoord0.y, texCoord0.x))
		: texture(sampler1, vec2(texCoord0.y, texCoord0.x));
	lit += zTex * texCoord1.z;

	// Sun lighting, light table entry 0 (asm: state.light[0]). dirOrPos holds
	// the raw world-space propagation direction, so the surface-to-light term
	// is its negation; the asm LIT clamps the diffuse term to [0, 1].
	float ndl = clamp(dot(n, -nlLights[0].dirOrPos), 0.0, 1.0);
	vec3 color = lit.rgb * (nlLights[0].ambient.rgb + nlLights[0].diffuse.rgb * ndl);

	// Fog, linear branch — identical to the GL3 builtin applyFog and to the
	// assembly's MAD_SAT/LRP pair: (fogParams.t - z) / (fogParams.t - fogParams.s)
	// with NeL-space eye depth z = |ecPos.y / ecPos.w|.
	float depth = abs(ecPos.y / ecPos.w);
	float fogFactor = clamp((fogParams.t - depth) / (fogParams.t - fogParams.s), 0.0, 1.0);
	color = mix(fogColor.rgb, color, fogFactor);

	// Alpha: per-vertex alpha times the mesh display factor (asm: c[2].z).
	// The texture-sampled alpha channels of 'lit' are discarded, exactly like
	// the asm's MOV R1.a / MUL R1.a at the end. Mode 6 pins alpha to 1 to
	// test whether the alpha chain itself is the production blackout.
	float alpha = texCoord2.w * landscapeParams.z;
	if (dbg == 6)
		alpha = 1.0;

	fragColor = vec4(color, alpha);
}
