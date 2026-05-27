#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNorm;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragTan;
layout(location = 4) in vec3 shadowPos;

layout(location = 0) out vec4 outColor;

layout(binding = 1, set = 1) uniform sampler2D albedoMap[4];
layout(binding = 2, set = 1) uniform sampler2D normalMap[4];
layout(binding = 3, set = 1) uniform sampler2D metallicMap[4];
layout(binding = 4, set = 1) uniform sampler2D roughnessMap[4];
layout(binding = 5, set = 1) uniform sampler2D aoMap[4];
layout(binding = 6, set = 1) uniform sampler2D shadowMap;

layout(binding = 0, set = 0) uniform GlobalUniformBufferObject {
    vec3 lightDir;
    vec4 lightColor;
    vec3 eyePos;
    vec4 debugView;
} gubo;

const float PI = 3.14159265359;

mat3 computeTBN(vec3 N, vec3 T, float tangentW) {
    vec3 B = cross(N, T) * tangentW;
    return mat3(normalize(T), normalize(B), normalize(N));
}

vec3 getNormalFromMap(mat3 TBN,int ti) {
    vec3 tangentNormal = texture(normalMap[ti], fragUV).xyz * 2.0 - 1.0;
    return normalize(TBN * tangentNormal);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = max((NdotH2 * (a2 - 1.0) + 1.0), 0.0001);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
    GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

void main() {
    int ti = int(gubo.debugView.z);

    vec3 albedo     = pow(texture(albedoMap[ti], fragUV).rgb, vec3(2.2)); 
    float metallic  = texture(metallicMap[ti], fragUV).r;
    float roughness = texture(roughnessMap[ti], fragUV).r;
    float ao        = texture(aoMap[ti], fragUV).r;


    vec3 N = normalize(fragNorm);
    vec3 T = normalize(fragTan.xyz);
    float w = fragTan.w;
    mat3 TBN = computeTBN(N, T, w);
    vec3 Nmap = getNormalFromMap(TBN,ti);

    vec3 V = normalize(gubo.eyePos - fragPos);
    vec3 L = normalize(-gubo.lightDir);
    vec3 H = normalize(V + L);
    vec3 radiance = gubo.lightColor.rgb;


    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = DistributionGGX(Nmap, H, roughness);
    float G   = GeometrySmith(Nmap, V, L, roughness);
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (NDF * G * F) /
    max(4.0 * max(dot(Nmap, V), 0.0) * max(dot(Nmap, L), 0.0), 0.001);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    float NdotL = max(dot(Nmap, L), 0.0);
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

	const vec3 cxp = vec3(1.0,0.5,0.5) * 0.1;
	const vec3 cxn = vec3(0.9,0.6,0.4) * 0.1;
	const vec3 cyp = vec3(0.3,1.0,1.0) * 0.1;
	const vec3 cyn = vec3(0.5,0.5,0.5) * 0.1;
	const vec3 czp = vec3(0.8,0.2,0.4) * 0.1;
	const vec3 czn = vec3(0.3,0.6,0.7) * 0.1;
	
	vec3 ambient =((Nmap.x > 0 ? cxp : cxn) * (Nmap.x * Nmap.x) +
				   (Nmap.y > 0 ? cyp : cyn) * (Nmap.y * Nmap.y) +
				   (Nmap.z > 0 ? czp : czn) * (Nmap.z * Nmap.z)) * albedo;	

	float lightMapDist = shadowPos.z;
	vec2 lightMapUV = (shadowPos.xy + 1.0f) / 2.0f;

	float shadowBias = max(0.012f * (1.0f - dot(Nmap, L)), 0.002f);
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

    vec3 color = ambient + Lo * notInShadow;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
