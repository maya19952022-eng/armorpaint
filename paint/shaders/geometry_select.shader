
cbuffer constants {
	float4x4 WVP;
	float4 color;
};

struct vert_in {
	float4 pos;
};

struct vert_out {
	float4 pos;
};

vert_out vert(vert_in input) {
	vert_out output;
	output.pos = constants.WVP * float4(input.pos.xyz, 1.0);
	// In front of the depth of the same faces, behind any other surface
	output.pos.z = output.pos.z - 0.0001 * output.pos.w;
	return output;
}

float4 frag(vert_out input) {
	return constants.color;
}
