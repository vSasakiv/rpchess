#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNorm;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 shadowPos;

layout(location = 0) out vec4 outColor;

layout(binding = 1, set = 1) uniform sampler2D tex;
layout(binding = 2, set = 1) uniform sampler2D shadowMap;

layout(binding = 0, set = 0) uniform GlobalUniformBufferObject {
	vec3 lightDir;
	vec4 lightColor;
	vec3 eyePos;
	vec4 debug;
} gubo;


const float PI = 3.14159265359;

// Normal Distribution function --------------------------------------
float D_GGX(float dotNH, float roughness)
{
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float denom = dotNH * dotNH * (alpha2 - 1.0f) + 1.0f;
	return (alpha2)/(PI * denom*denom); 
}

// Geometric Shadowing function --------------------------------------
float G_SchlicksmithGGX(float dotNL, float dotNV, float roughness)
{
	float r = (roughness + 1.0f);
	float k = (r*r) / 8.0f;
	float GL = dotNL / (dotNL * (1.0f - k) + k);
	float GV = dotNV / (dotNV * (1.0f - k) + k);
	return GL * GV;
}

// Fresnel function ----------------------------------------------------
vec3 F_Schlick(float cosTheta, float metallic, vec3 materialcolor)
{
	vec3 F0 = mix(vec3(0.04f), materialcolor, metallic); // * material.specular
	vec3 F = F0 + (vec3(1.0f) - F0) * pow(1.0f - cosTheta, 5.0f); 
	return F;    
}

// Specular BRDF composition --------------------------------------------

vec3 BRDF(vec3 L, vec3 V, vec3 N, float metallic, float roughness, vec3 materialcolor)
{
	// Precalculate vectors and dot products	
	vec3 H = normalize (V + L);
	float dotNV = clamp(dot(N, V), 0.0f, 1.0f);
	float dotNL = clamp(dot(N, L), 0.0f, 1.0f);
	float dotLH = clamp(dot(L, H), 0.0f, 1.0f);
	float dotNH = clamp(dot(N, H), 0.0f, 1.0f);

	vec3 color = vec3(0.0f);

	if (dotNL > 0.0f)
	{
		float rroughness = max(0.05f, roughness);
		// D = Normal distribution (Distribution of the microfacets)
		float D = D_GGX(dotNH, roughness); 
		// G = Geometric shadowing term (Microfacets shadowing)
		float G = G_SchlicksmithGGX(dotNL, dotNV, rroughness);
		// F = Fresnel factor (Reflectance depending on angle of incidence)
		vec3 F = F_Schlick(dotNV, metallic, materialcolor);

		vec3 spec = D * F * G / (4.0f * dotNV);

		color += spec;
	}

	return color;
}

void main() {
	vec3 Norm = normalize(fragNorm);
	vec3 EyeDir = normalize(gubo.eyePos - fragPos);
	
	vec3 lightDir = -gubo.lightDir;
	vec3 lightColor = gubo.lightColor.rgb;
	vec3 albedo = texture(tex, fragUV).rgb;
	
	vec3 Diffuse = albedo * clamp(dot(Norm, lightDir),0.0f,1.0f);
//	vec3 Specular = vec3(pow(clamp(dot(Norm, normalize(lightDir + EyeDir)),0.0,1.0), 160.0f));
	vec3 Specular = BRDF(lightDir, EyeDir, Norm, 0.9f, 0.2f, albedo);
	
	const vec3 cxp = vec3(1.0,0.5,0.5) * 0.15;
	const vec3 cxn = vec3(0.9,0.6,0.4) * 0.15;
	const vec3 cyp = vec3(0.3,1.0,1.0) * 0.15;
	const vec3 cyn = vec3(0.5,0.5,0.5) * 0.15;
	const vec3 czp = vec3(0.8,0.2,0.4) * 0.15;
	const vec3 czn = vec3(0.3,0.6,0.7) * 0.15;
	
	vec3 Ambient =((Norm.x > 0 ? cxp : cxn) * (Norm.x * Norm.x) +
				   (Norm.y > 0 ? cyp : cyn) * (Norm.y * Norm.y) +
				   (Norm.z > 0 ? czp : czn) * (Norm.z * Norm.z)) * albedo;	

	float lightMapDist = shadowPos.z;
	vec2 lightMapUV = (shadowPos.xy + 1.0f) / 2.0f;

	float shadowBias = max(0.012f * (1.0f - dot(Norm, lightDir)), 0.002f);
	const float shadowScale = 128.0f;
	vec2 texelSize = 1.0f / vec2(textureSize(shadowMap, 0));
	float notInShadow = 0.0f;
	for(int x = -1; x <= 1; x++) {
		for(int y = -1; y <= 1; y++) {
			float depth = texture(shadowMap, lightMapUV + vec2(x, y) * texelSize).r;
			notInShadow += depth + shadowBias >= lightMapDist ? 1.0f : 0.0f;
		}

	}
	notInShadow /= 9.0f;

	vec3 col  = (Diffuse + Specular) * lightColor * notInShadow + Ambient;
	//vec3 col  = (Diffuse + Specular) * lightColor + Ambient;
	//vec3 col = vec3(texture(shadowMap, fragUV));
	//vec3 col = shadowPos;
	
	//outColor = vec4(lightMapUV, 0, 1.0f);
	outColor = vec4(col,1);
}