/********************************************************************
Vireio Perception: Open-Source Stereoscopic 3D Driver
Copyright (C) 2012 Andres Hernandez

File <ShaderConstantModificationFactory.h> and
Class <ShaderConstantModificationFactory> :
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

#ifndef SHADERCONSTANTMODIFICATIONFACTORY_H_INCLUDED
#define SHADERCONSTANTMODIFICATIONFACTORY_H_INCLUDED

#include <memory>
#include "d3d9.h"
#include "d3dx9.h"
#include "Vector4SimpleTranslate.h"
#include "ShaderConstantModification.h"
#include "ShaderMatrixModification.h"
#include "MatrixIgnoreOrtho.h"
#include "MatrixDoNothing.h"
#include "MatrixHudSquash.h"
#include "MatrixOrthoSquash.h"
#include "MatrixOrthoSquashHud.h"
#include "MatrixOrthoSquashShifted.h"
#include "MatrixSurfaceRefractionTransform.h"
#include "MatrixGatheredOrhoSquash.h"

/**
* Shader constant modification helper class.
* Contains static functions to create modifications and modification enumerations.
*/
class ShaderConstantModificationFactory
{
public:
	/**
	* Vector4 modification identifiers. 
	***/
	enum Vector4ModificationTypes
	{
		Vec4DoNothing = 0,                 /**< Simple modification that does not apply anything. **/
		Vec4SimpleTranslate = 1            /**< Default modification is simple translate. **/
	};
	/**
	* Matrix modification identifiers.
	***/
	enum MatrixModificationTypes
	{
		MatDoNothing = 0,                  /**< Simple modification that does not apply anything. **/
		MatSimpleTranslate = 1,            /**< Default modification is simple translate. **/
		MatOrthographicSquash = 2,         /**< Squashes matrix if orthographic, otherwise simple translate. **/
		MatSimpleTranslateIgnoreOrtho = 3, /**< Modification to ignore orthographic matrices. **/
		MatHudSquash = 4,                  /**< Modification to squash the head-up display(HUD). **/
		MatSurfaceRefractionTransform = 5, /**< Modification to fix surface refraction in pixel shaders. **/
		MatGatheredOrthographicSquash = 6, /**< Squashes matrix if orthographic, otherwise simple translate. Result will be gathered to be used in other modifications.**/
		MatOrthographicSquashShifted = 7,  /**< Squashes matrix if orthographic, otherwise simple translate - shift accordingly. **/
		MatOrthographicSquashHud = 8       /**< Squashes matrix if orthographic, otherwise simple translate - matrices treated as beeing for HUD. **/
	};

	/**
	* Calls twin function.
	***/
	static std::shared_ptr<ShaderConstantModification<>> CreateVector4Modification(UINT modID, std::shared_ptr<ViewAdjustment> adjustmentMatricies)
	{
		return CreateVector4Modification(static_cast<Vector4ModificationTypes>(modID), adjustmentMatricies);
	}
	/**
	* Creates Vector4 modification.
	* @see Vector4SimpleTranslate
	***/
	static std::shared_ptr<ShaderConstantModification<>> CreateVector4Modification(Vector4ModificationTypes mod, std::shared_ptr<ViewAdjustment> adjustmentMatricies)
	{
		switch (mod)
		{
		case Vec4SimpleTranslate:
			return std::make_shared<Vector4SimpleTranslate>(mod, adjustmentMatricies);

		default:
			OutputDebugString("Nonexistant Vec4 modification\n");
			assert(false);
			throw std::out_of_range ("Nonexistant Vec4 modification");
		}
	}
	/**
	* Calls twin function.
	***/
	static std::shared_ptr<ShaderConstantModification<>> CreateMatrixModification(UINT modID, std::shared_ptr<ViewAdjustment> adjustmentMatricies, bool transpose) 
	{
		return CreateMatrixModification(static_cast<MatrixModificationTypes>(modID), adjustmentMatricies, transpose);
	}
	/**
	* Creates matrix modification.
	* @see MatrixDoNothing
	* @see ShaderMatrixModification
	* @see MatrixIgnoreOrtho
	* @see MatrixHudSquash
	***/
	static std::shared_ptr<ShaderConstantModification<>> CreateMatrixModification(MatrixModificationTypes mod, std::shared_ptr<ViewAdjustment> adjustmentMatricies, bool transpose)
	{
		switch (mod)
		{
		case MatDoNothing:
			return std::make_shared<MatrixDoNothing>(mod, adjustmentMatricies);

		case MatSimpleTranslate:
			return std::make_shared<ShaderMatrixModification>(mod, adjustmentMatricies, transpose);

		case MatOrthographicSquash:
			return std::make_shared<MatrixOrthoSquash>(mod, adjustmentMatricies, transpose);

		case MatSimpleTranslateIgnoreOrtho:
			return std::make_shared<MatrixIgnoreOrtho>(mod, adjustmentMatricies, transpose);

		case MatHudSquash:
			return std::make_shared<MatrixHudSquash>(mod, adjustmentMatricies, transpose);

		case MatSurfaceRefractionTransform:
			return std::make_shared<MatrixSurfaceRefractionTransform>(mod, adjustmentMatricies, transpose);

		case MatGatheredOrthographicSquash:
			return std::make_shared<MatrixGatheredOrthoSquash>(mod, adjustmentMatricies, transpose);

		case MatOrthographicSquashShifted:
			return std::make_shared<MatrixOrthoSquashShifted>(mod, adjustmentMatricies, transpose);

		case MatOrthographicSquashHud:
			return std::make_shared<MatrixOrthoSquashHud>(mod, adjustmentMatricies, transpose);

		default:
			OutputDebugString("Nonexistant matrix modification\n");
			assert(false);
			throw std::out_of_range ("Nonexistant matrix modification");
		}
	}
};
#endif