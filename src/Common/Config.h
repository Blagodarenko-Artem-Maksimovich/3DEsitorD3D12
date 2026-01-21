#pragma once

#include <windows.h>
#include <DirectXMath.h>
using namespace DirectX;

struct Config
{
	// Shadow Map
	UINT ShadowMapWidth = 2048;
	UINT ShadowMapHeight = 2048;

	// Camera
	float CameraWalkSpeed = 10.0f;
	float CameraRotationSpeed = 0.25f;
	float CameraNearZ = 1.0f;
	float CameraFarZ = 1000.0f;
	float CameraFovY = 0.25f * DirectX::XM_PI;

	// Lighting
	float LightRotationSpeed = 0.1f;
	XMFLOAT3 BaseLightDirections[3] = {
		XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
		XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
		XMFLOAT3(0.0f, -0.707f, -0.707f)
	};
	XMFLOAT3 RotatedLightDirections[3];
	// Scene
	float SceneBoundsRadius = 18.0f;
	DirectX::XMFLOAT3 SceneBoundsCenter = { 0.0f, 0.0f, 0.0f };

};
	
