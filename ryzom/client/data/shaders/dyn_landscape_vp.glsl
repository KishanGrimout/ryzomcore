// DynLandscape vertex stage — hand-authored GLSL port of dyn_landscape.vp1 (!!VP1.0).
//
// PROFILE-NEUTRAL BODY: no #version, no attribute/varying declarations, no
// precision statements and no uniform block declarations in this file.
// CDynLandscape::setupPrograms() prepends the per-profile prefix
// (glsl330 SSO or glsl300es linked) that declares the attributes/varyings, and
// the GL3 driver injects the builtin uniform blocks (NlModel + NlCamera via
// Features.UsesObjectUBO) and the user "NlDynLandscape" params block
// (insertBuiltinHeaders) — none of those may be declared here.
//
// Consumed builtins  : modelViewProjection (NlModel), inverseProjectionBasis
//                      (NlCamera); user UBO member landscapeParams
//                      (vec4: x = tighten, y = scale, z = displayFactor, w = spare).
// Consumed attributes: position (loc 0), normal (loc 2), empty (loc 7 —
//                      the deprecated Empty alias IS Tangent, enum 15; x =
//                      per-vertex alpha carried in the CRegionSubMesh
//                      Tangent/Empty pad slot = legacy vp1 v[7]).
// Outputs            : gl_Position, ecPos (NeL-space eye position, loc 0),
//                      texCoord0/1/2 (locs 8/9/10, same varying contract as
//                      the nelvp->GLSL converter and the builtin mega PP).
//
// Semantics ported from the assembly:
//   o[HPOS] = c[0..3] * v[OPOS]                     → gl_Position
//   o[TEX0] = v[OPOS] * c[5].xxxx (scale)           → texCoord0
//   o[TEX1] = triplanar blend weights from v[NRML]  → texCoord1
//   o[TEX2] = (v[NRML].xyz, w = alpha)              → texCoord2
// The nelvp o[FOGC] output is intentionally NOT reproduced: GL3 fog is
// computed in the fragment stage from ecPos (dyn_landscape_fp.glsl), matching
// the builtin mega PP fog. The GL1/nelvp profile keeps its own o[FOGC] logic.

void main()
{
	// o[HPOS]: MVP transform (NlModel UBO, uploaded per draw call by the driver).
	gl_Position = modelViewProjection * position;

	// Synthesized ecPos — same convention as the nelvp->GLSL converter:
	// inv(Projection * ChangeBasis) * gl_Position = object position in NeL
	// eye space (fog depth axis = ecPos.y in NeL space for the fragment fog).
	ecPos = inverseProjectionBasis * gl_Position;

	// o[TEX0] = scaled object position (asm: MUL R0, R0, c[5].xxxx)
	texCoord0 = vec4(position.xyz * landscapeParams.y, 0.0);

	// o[TEX2] = (object normal, w = per-vertex alpha).
	// PARITY NOTE (corrected): the asm reads "MOV R0.w, v[7].x". In the GL3
	// attribute map slot 7 = Tangent, and CVertexBuffer::Empty — which
	// region_mesh.cpp uses for the alpha — is a DEPRECATED ALIAS of Tangent
	// (enum 15), so the data physically rides the Tangent slot: GL index 6
	// (PaletteSkin) is never enabled for region VBs and reads GL's default
	// constant (0,0,0,1). Reading location 7 reproduces the assembly
	// exactly; the original hand-port read location 6 and produced an
	// all-zero alpha (production mode rendered invisible through blending).
	texCoord2 = vec4(normal.xyz, empty.x);

	// o[TEX1]: blending factors from the normal
	// absolute value of the normal (asm: MAX R0, R1, -R1)
	vec3 w = abs(normal.xyz);
	// tighten (asm: ADD R0, R0, -c[4].xxxx; MAX R0, R0, c[4].yyyy — the
	// same 'tighten' uniform on both steps)
	w = max(w - landscapeParams.x, vec3(0.0));
	// amplification: three successive squarings (asm MUL x,3 -> component^8)
	w *= w; w *= w; w *= w;
	// divide each of the 3 components by their sum (asm: per-component RCP
	// then MUL). Deliberate no-zero-guard parity: planar normals give a
	// zero-sum floor component whose weight is discarded by the fragment
	// stage, exactly as in the assembly.
	float weightSum = w.x + w.y + w.z;
	w /= vec3(weightSum);
	texCoord1 = vec4(w, 0.0); // asm o[TEX1].w is garbage; never read
}
