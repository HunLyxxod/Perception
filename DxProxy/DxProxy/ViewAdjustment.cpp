/********************************************************************
Vireio Perception: Open-Source Stereoscopic 3D Driver
Copyright (C) 2012 Andres Hernandez

File <ViewAdjustment.cpp> and
Class <ViewAdjustment> :
Copyright (C) 2013 Chris Drain

Vireio Perception Version History:
v1.0.0 2012 by Andres Hernandez
v1.0.X 2013 by John Hicks, Neil Schneider
v1.1.x 2013 by Primary Coding Author: Chris Drain
Team Support: John Hicks, Phil Larkson, Neil Schneider
v2.0.x 2013 by Denis Reischl, Neil Schneider, Joshua Brown

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
********************************************************************/


#include "ViewAdjustment.h"

/**
* Constructor.
* Sets class constants, identity matrices and a projection matrix.
***/
ViewAdjustment::ViewAdjustment(HMDisplayInfo &displayInfo, float metersToWorldUnits, bool enableRoll) :
	hmdInfo(displayInfo),
	metersToWorldMultiplier(metersToWorldUnits),
	rollEnabled(enableRoll),
	bulletLabyrinth(false)
{
	// TODO : max, min convergence; arbitrary now
	convergence = 0.0f;
	minConvergence = -10.0f;
	maxConvergence = 10.0f;

	ipd = IPD_DEFAULT;

	n = 0.1f;					
	f = 10.0f;
	l = -0.5f;
	r = 0.5f;

	squash = 1.0f;
	gui3DDepth = 0.0f;
	hudDistance = 0.0f;
	hud3DDepth = 0.0f;

	D3DXMatrixIdentity(&matProjection);
	D3DXMatrixIdentity(&matProjectionInv);
	D3DXMatrixIdentity(&leftShiftProjection);
	D3DXMatrixIdentity(&rightShiftProjection);
	D3DXMatrixIdentity(&projectLeft);
	D3DXMatrixIdentity(&projectRight);
	D3DXMatrixIdentity(&transformLeft);
	D3DXMatrixIdentity(&transformRight);
	D3DXMatrixIdentity(&matViewProjTransformRight);
	D3DXMatrixIdentity(&matViewProjTransformLeft);
	D3DXMatrixIdentity(&matGatheredLeft);
	D3DXMatrixIdentity(&matGatheredRight);

	UpdateProjectionMatrices(displayInfo.screenAspectRatio);
	D3DXMatrixIdentity(&rollMatrix);
	ComputeViewTransforms();
}

/**
* Empty destructor.
***/
ViewAdjustment::~ViewAdjustment() 
{
}

/**
* Loads game configuration data.
* @param cfg Game configuration to load.
***/
void ViewAdjustment::Load(ProxyHelper::ProxyConfig& cfg) 
{
	rollEnabled = cfg.rollEnabled;
	metersToWorldMultiplier  = cfg.worldScaleFactor;
	convergence = cfg.convergence;
	ipd = cfg.ipd;
	stereoType = cfg.stereo_mode;
}

/**
* Saves game configuration data.
* @param cfg The game configuration to be saved to.
***/
void ViewAdjustment::Save(ProxyHelper::ProxyConfig& cfg) 
{
	cfg.rollEnabled = rollEnabled;
	cfg.convergence = convergence;

	//worldscale and ipd are not normally edited;
	cfg.worldScaleFactor = metersToWorldMultiplier;
	cfg.ipd = ipd;
}

/**
* Updates left and right projection matrices.
* Now, the convergence point is specified in real, physical meters, since the IPD is also specified
* in physical meters. That means, if the game-specific world scale is set correctly, a convergence
* value of 3.0f would mean that the virtual screen, neutral point or convergence point is 3 meters
* ahead of us.
* @param aspectRation The aspect ratio for the projection matrix.
***/
void ViewAdjustment::UpdateProjectionMatrices(float aspectRatio)
{
	t = 0.5f / aspectRatio;
	b = -0.5f / aspectRatio;

	D3DXMatrixPerspectiveOffCenterLH(&matProjection, l, r, b, t, n, f);
	D3DXMatrixInverse(&matProjectionInv, 0, &matProjection);

	// convergence frustum adjustment, based on NVidia explanations
	//
	// It is evident that the ratio of frustum shift to the near clipping plane is equal to the ratio of 
	// IOD/2 to the distance from the screenplane. (IOD=IPD) 
	// frustumAsymmetryInMeters = ((IPD/2) * nearClippingPlaneDistance) / convergence
	// <http://www.orthostereo.com/geometryopengl.html>
	//
	// (near clipping plane distance = physical screen distance)
	// (convergence = virtual screen distance)
	// ALL stated in meters here !
	const float nearClippingPlaneDistance = 1; // < TODO !! Assumption here : near clipping plane distance = 1 meter 
	if (convergence <= nearClippingPlaneDistance) convergence = nearClippingPlaneDistance + 0.001f;
	float frustumAsymmetryInMeters = ((ipd/2) * nearClippingPlaneDistance) / convergence;

	// divide the frustum asymmetry by the assumed physical size of the physical screen
	const float physicalScreenSizeInMeters = 1; // < TODO !! Assumption here : physical screen size = 1 meter
	float frustumAsymmetryLeftInMeters = (frustumAsymmetryInMeters * LEFT_CONSTANT) / physicalScreenSizeInMeters;
	float frustumAsymmetryRightInMeters = (frustumAsymmetryInMeters * RIGHT_CONSTANT) / physicalScreenSizeInMeters;

	// get the horizontal screen space size and compute screen space adjustment
	float screenSpaceXSize = abs(l)+abs(r);
	float multiplier = screenSpaceXSize/1; // = 1 meter
	float frustumAsymmetryLeft = frustumAsymmetryLeftInMeters * multiplier;
	float frustumAsymmetryRight = frustumAsymmetryRightInMeters * multiplier;

	// now, create the re-projection matrices for both eyes using this frustum asymmetry
	D3DXMatrixPerspectiveOffCenterLH(&projectLeft, l+frustumAsymmetryLeft, r+frustumAsymmetryLeft, b, t, n, f);
	D3DXMatrixPerspectiveOffCenterLH(&projectRight, l+frustumAsymmetryRight, r+frustumAsymmetryRight, b, t, n, f);

	// Based on Rift docs way.
	if ((stereoType == 26) || // = StereoView::StereoTypes::OCULUS_RIFT
		(stereoType == 27) || // = StereoView::StereoTypes::OCULUS_RIFT_CROPPED
		(stereoType == 25))   // = StereoView::StereoTypes::DIY_RIFT))
	{
		// The lensXCenterOffset is in the same -1 to 1 space as the perspective so shift by that amount to move projection in line with the lenses
		D3DXMatrixTranslation(&leftShiftProjection, hmdInfo.lensXCenterOffset * LEFT_CONSTANT, 0, 0);
		D3DXMatrixTranslation(&rightShiftProjection, hmdInfo.lensXCenterOffset * RIGHT_CONSTANT, 0, 0);
		projectLeft *= leftShiftProjection;
		projectRight *= rightShiftProjection;
	}	
}

/**
* Updates the current pitch and yaw head movement.
***/
void ViewAdjustment::UpdatePitchYaw(float pitch, float yaw)
{
	// bullet labyrinth matrix
	/*float yawRad = D3DXToRadian(yaw);
	float pitchRad = D3DXToRadian(pitch);*/
	D3DXMatrixTranslation(&matBulletLabyrinth, -yaw, pitch, 0.0f);
}

/**
* Updates the roll matrix, seems to be senseless right now, just calls D3DXMatrixRotationZ().
* @param roll Angle of rotation, in radians.
***/
void ViewAdjustment::UpdateRoll(float roll)
{
	D3DXMatrixIdentity(&rollMatrix);
	D3DXMatrixRotationZ(&rollMatrix, roll);
}

/**
* Gets the view-projection transform matrices left and right.
* Unprojects, shifts view position left/right (using same matricies as (Left/Right)ViewRollAndShift)
* and reprojects using left/right projection.
* (matrix = projectionInverse * transform * projection)
***/
void ViewAdjustment::ComputeViewTransforms()
{
	// if (HMD)
	D3DXMatrixTranslation(&transformLeft, SeparationInWorldUnits() * LEFT_CONSTANT, 0, 0);
	D3DXMatrixTranslation(&transformRight, SeparationInWorldUnits() * RIGHT_CONSTANT, 0, 0);
	// else if desktop screen {}

	if (rollEnabled) {
		D3DXMatrixMultiply(&transformLeft, &rollMatrix, &transformLeft);
		D3DXMatrixMultiply(&transformRight, &rollMatrix, &transformRight);
	}

	matViewProjTransformLeft = matProjectionInv * transformLeft * projectLeft;
	matViewProjTransformRight = matProjectionInv * transformRight * projectRight;

	// now, create HUD/GUI helper matrices

	// squash
	D3DXMatrixScaling(&matSquash, squash, squash, 1);

	// hudDistance
	D3DXMatrixTranslation(&matHudDistance, 0, 0, hudDistance);

	// hud3DDepth
	D3DXMatrixTranslation(&matLeftHud3DDepth, hud3DDepth, 0, 0);
	D3DXMatrixTranslation(&matRightHud3DDepth, -hud3DDepth, 0, 0);
	float additionalSeparation = (1.5f-hudDistance)*hmdInfo.lensXCenterOffset;
	D3DXMatrixTranslation(&matLeftHud3DDepthShifted, hud3DDepth+additionalSeparation, 0, 0);
	D3DXMatrixTranslation(&matRightHud3DDepthShifted, -hud3DDepth-additionalSeparation, 0, 0);
	D3DXMatrixTranslation(&matLeftGui3DDepth, gui3DDepth+SeparationIPDAdjustment(), 0, 0);
	D3DXMatrixTranslation(&matRightGui3DDepth, -(gui3DDepth+SeparationIPDAdjustment()), 0, 0);
}

/**
* Returns the left view projection transform matrix.
***/
D3DXMATRIX ViewAdjustment::LeftAdjustmentMatrix()
{
	return matViewProjTransformLeft;
}

/**
* Returns the right view projection transform matrix.
***/
D3DXMATRIX ViewAdjustment::RightAdjustmentMatrix()
{
	return matViewProjTransformRight;
}

/**
* Returns the left matrix used to roll (if roll enabled) and shift view for ipd.
***/
D3DXMATRIX ViewAdjustment::LeftViewTransform()
{
	return transformLeft;
}

/**
* Returns the right matrix used to roll (if roll enabled) and shift view for ipd.
***/
D3DXMATRIX ViewAdjustment::RightViewTransform()
{
	return transformRight;
}

/**
* Returns the left shifted projection.
* (projection * This shift = left/right shifted projection)
***/
D3DXMATRIX ViewAdjustment::LeftShiftProjection()
{
	return leftShiftProjection;
}

/**
* Returns the right shifted projection.
* (projection * This shift = left/right shifted projection) 
***/
D3DXMATRIX ViewAdjustment::RightShiftProjection()
{
	return rightShiftProjection;
}

/**
* Return the current projection matrix.
***/
D3DXMATRIX ViewAdjustment::Projection()
{
	return matProjection;
}

/**
* Returns the current projection inverse matrix.
***/
D3DXMATRIX ViewAdjustment::ProjectionInverse()
{
	return matProjectionInv;
}

/**
* Returns the current projection inverse matrix.
***/
D3DXMATRIX ViewAdjustment::Squash()
{
	return matSquash;
}

/**
* Returns the current projection inverse matrix.
***/
D3DXMATRIX ViewAdjustment::HUDDistance()
{
	return matHudDistance;
}

/**
* Returns the current left HUD depth eye separation matrix.
***/
D3DXMATRIX ViewAdjustment::LeftHUD3DDepth()
{
	return matLeftHud3DDepth;
}

/**
* Returns the current left HUD depth eye separation matrix.
***/
D3DXMATRIX ViewAdjustment::RightHUD3DDepth()
{
	return matRightHud3DDepth;
}

/**
* Returns the current left HUD depth eye separation matrix shifted.
***/
D3DXMATRIX ViewAdjustment::LeftHUD3DDepthShifted()
{
	return matLeftHud3DDepthShifted;
}

/**
* Returns the current left HUD depth eye separation matrix shifted.
***/
D3DXMATRIX ViewAdjustment::RightHUD3DDepthShifted()
{
	return matRightHud3DDepthShifted;
}

/**
* Returns the current left HUD depth eye separation matrix.
***/
D3DXMATRIX ViewAdjustment::LeftGUI3DDepth()
{
	return matLeftGui3DDepth;
}

/**
* Returns the current left HUD depth eye separation matrix.
***/
D3DXMATRIX ViewAdjustment::RightGUI3DDepth()
{
	return matRightGui3DDepth;
}

/**
* Returns the current bullet labyrinth matrix.
***/
D3DXMATRIX ViewAdjustment::BulletLabyrinth()
{
	return matBulletLabyrinth;
}

/**
* Returns the current left gathered matrix.
***/
D3DXMATRIX ViewAdjustment::GatheredMatrixLeft()
{
	return matGatheredLeft;
}

/**
* Returns the current right gathered matrix.
***/
D3DXMATRIX ViewAdjustment::GatheredMatrixRight()
{
	return matGatheredRight;
}

/**
* Gathers a matrix to be used in modifications.
***/
void ViewAdjustment::GatherMatrix(D3DXMATRIX& matrixLeft, D3DXMATRIX& matrixRight)
{
	matGatheredLeft = D3DXMATRIX(matrixLeft);
	matGatheredRight = D3DMATRIX(matrixRight);
}

/**
* Returns the current world scale.
***/
float ViewAdjustment::WorldScale()
{
	return metersToWorldMultiplier;
}

/**
* Modifies the world scale with its limits 0.01f and 1,000,000 (arbitrary limit).
* NOTE: This should not be changed during normal usage, this is here to facilitate finding a reasonable scale.
***/
float ViewAdjustment::ChangeWorldScale(float toAdd)
{
	metersToWorldMultiplier+= toAdd;

	vireio::clamp(&metersToWorldMultiplier, 0.0001f, 1000000.0f);

	return metersToWorldMultiplier;
}

/**
* Changes and clamps convergence.
***/
float ViewAdjustment::ChangeConvergence(float toAdd)
{
	convergence += toAdd;

	vireio::clamp(&convergence, minConvergence, maxConvergence);

	return convergence;
}

/**
* Changes GUI squash and updates matrix.
***/
void ViewAdjustment::ChangeGUISquash(float newSquash)
{
	squash = newSquash;

	D3DXMatrixScaling(&matSquash, squash, squash, 1);
}

/**
* Changes the GUI eye separation (=GUI 3D Depth) and updates matrices.
***/
void ViewAdjustment::ChangeGUI3DDepth(float newGui3DDepth)
{
	gui3DDepth = newGui3DDepth;

	D3DXMatrixTranslation(&matLeftGui3DDepth, gui3DDepth+SeparationIPDAdjustment(), 0, 0);
	D3DXMatrixTranslation(&matRightGui3DDepth, -(gui3DDepth+SeparationIPDAdjustment()), 0, 0);
}

/**
* Changes the distance of the HUD and updates matrix.
***/
void ViewAdjustment::ChangeHUDDistance(float newHudDistance)
{
	hudDistance = newHudDistance;

	D3DXMatrixTranslation(&matHudDistance, 0, 0, hudDistance);
}

/**
*  Changes the HUD eye separation (=HUD 3D Depth) and updates matrices.
***/
void ViewAdjustment::ChangeHUD3DDepth(float newHud3DDepth)
{
	hud3DDepth = newHud3DDepth;

	D3DXMatrixTranslation(&matLeftHud3DDepth, -hud3DDepth, 0, 0);
	D3DXMatrixTranslation(&matRightHud3DDepth, hud3DDepth, 0, 0);
	float additionalSeparation = (1.5f-hudDistance)*hmdInfo.lensXCenterOffset;
	D3DXMatrixTranslation(&matLeftHud3DDepthShifted, hud3DDepth+additionalSeparation, 0, 0);
	D3DXMatrixTranslation(&matRightHud3DDepthShifted, -hud3DDepth-additionalSeparation, 0, 0);
}

/**
* Set to true if orthographical matrices should be rotated in a bullet labyrinth style.
***/
void ViewAdjustment::SetBulletLabyrinthMode(bool newMode)
{
	bulletLabyrinth = newMode;
}

/**
* True if bullet-labyrinth mode is on.
***/
bool ViewAdjustment::BulletLabyrinthMode()
{
	return bulletLabyrinth;
}

/**
* Just sets world scale to 3.0f.
***/
void ViewAdjustment::ResetWorldScale()
{
	metersToWorldMultiplier = 3.0f;
}

/**
* Just sets convergence to 3.0f (= 3 physical meters).
***/
void ViewAdjustment::ResetConvergence()
{
	convergence = 3.0f;
}

/**
* Returns the current convergence adjustment, in meters.
***/
float ViewAdjustment::Convergence() 
{ 
	return convergence; 
}

/**
* Returns the current convergence adjustment, in game units.
***/
float ViewAdjustment::ConvergenceInWorldUnits() 
{ 
	return convergence * metersToWorldMultiplier; 
}

/**
* Returns the separation being used for view adjustments in game units.
***/
float ViewAdjustment::SeparationInWorldUnits() 
{ 
	return  (ipd / 2.0f) * metersToWorldMultiplier; 
}

/**
* Returns the separation IPD adjustment being used for GUI and HUD matrices.
* (or whenever the eye separation is set manually)
***/
float ViewAdjustment::SeparationIPDAdjustment() 
{ 
	return  ((ipd-IPD_DEFAULT) / 2.0f) * metersToWorldMultiplier; 
}

/**
* Returns true if head roll is enabled.
***/
bool ViewAdjustment::RollEnabled() 
{ 
	return rollEnabled; 
}

/**
* Returns the head mounted display info.
***/
HMDisplayInfo ViewAdjustment::HMDInfo()
{
	return hmdInfo;
}