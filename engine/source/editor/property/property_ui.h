#pragma once

#include "editor/base/editor_ui.h"

namespace rttr
{
	class type;
	class variant;
}

namespace Bamboo
{
	enum class EPropertyValueType
	{
		Bool, Integar, Float, String, Vec2, Vec3, Vec4, Asset
	};

	enum class EPropertyContainerType
	{
		Mono, Array, Map
	};

	using EPropertyType = std::pair<EPropertyValueType, EPropertyContainerType>;

	class PropertyUI : public EditorUI
	{
	public:
		virtual void init() override;
		virtual void construct() override;
		virtual void destroy() override;

	private:
		void onSelectEntity(const std::shared_ptr<class Event>& event);

		void constructComponent(const std::shared_ptr<class Component>& component);

		void constructPropertyBool(const std::string& name, rttr::variant& variant);
		void constructPropertyIntegar(const std::string& name, rttr::variant& variant);
		void constructPropertyFloat(const std::string& name, rttr::variant& variant);
		void constructPropertyString(const std::string& name, rttr::variant& variant);
		void constructPropertyVec2(const std::string& name, rttr::variant& variant);
		void constructPropertyVec3(const std::string& name, rttr::variant& variant);
		void constructPropertyVec4(const std::string& name, rttr::variant& variant);
		void constructPropertyAsset(const std::string& name, rttr::variant& variant);

		EPropertyType getPropertyType(const rttr::type& type);

		std::shared_ptr<class Entity> m_selected_entity;
		std::map<EPropertyValueType, std::function<void(const std::string&, rttr::variant&)>> m_property_constructors;
	};
}