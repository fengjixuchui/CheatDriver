#pragma once

/*
* https://github.com/rdtnjntrsed/Rust-Internal-Always-Updated/blob/7b0a954c2fa31b562f06edbd20b29437913c7e01/Rust-Internal-Always-Updated-main/rust%20base/game/il2cpp.cpp
*/
#include <Windows.h>
#include <string>
#include <unordered_map>
#include <array>
#include <intrin.h>

#include "internal.hpp"
#include "lazy_importer.hpp"
#include "memory.hpp"
#include "vec3.hpp"

#define xorstr_(x) (x)

template<typename T1, typename T2>
bool map_contains_key(T1 map, T2 key) {
	return map.count(key) > 0;
}
float get_lowest(std::vector<float> input) {
	float ret = FLT_MAX;

	for (float idx : input) {
		if (idx < ret)
			ret = idx;
	}

	return ret;
}

auto gameAssembly = GetModuleHandleA(unprotect_str_const("GameAssembly.dll"));
#define ProcAddr(func) GetProcAddress(gameAssembly, unprotect_str_const(func))
#define CALLED_BY(func,off) (reinterpret_cast<std::uint64_t>(_ReturnAddress()) > func && reinterpret_cast<std::uint64_t>(_ReturnAddress()) < func + off)

template<typename T, typename... Args>
inline T call(const char* func, Args... args) {
	return reinterpret_cast<T(__stdcall*)(Args...)>(ProcAddr(func))(args...);
}

template<typename T, typename... Args>
inline T call(uintptr_t func, Args... args) {
	return reinterpret_cast<T(__stdcall*)(Args...)>(func)(args...);
}

template<typename T, typename... Args>
inline T call(FARPROC func, Args... args) {
	return reinterpret_cast<T(__stdcall*)(Args...)>(func)(args...);
}

class Transform;
bool LineOfSight(Vector3, Vector3);

namespace System {
	class Object_ {
	public:

	};
	template<typename T = void*>
	class Array {
	public:
		uint32_t size() {
			if (!this) return 0;
			return *reinterpret_cast<uint32_t*>(this + 0x18);
		}
		T get(int idx) {
			if (!this) return T{};
			return *reinterpret_cast<T*>(this + (0x20 + (idx * 0x8)));
		}
		void add(int idx, T value) {
			if (!this) return;
			*reinterpret_cast<T*>(this + (0x20 + (idx * 0x8))) = value;
		}
	};
	class String : public Object_ {
	public:
		char pad_0000[0x10];
		int len;
		wchar_t buffer[0];

		static String* New(const char* str) {
			static const auto off = ProcAddr("il2cpp_string_new");
			return call<String*, const char*>(off, str);
		}
	};
	template<typename T = void*>
	struct List {
	public:
		char pad_0000[0x10];
		void* buffer;
		uint32_t size;

		T* get(uint32_t idx) {
			if (!this) return nullptr;

			if (idx > this->size) return nullptr;

			void* items = this->buffer;

			if (!items) return nullptr;

			return *reinterpret_cast<T**>((uint64_t)items + (0x20 + (idx * 0x8)));
		}
	};

	typedef struct Str
	{
		char stub[0x10];
		int len;
		wchar_t str[1];
	} *str;
	class ListDictionary
	{
	public:
		char pad_0000[0x20];
		class BufferList* keys;
		class BufferList* vals;
	};
	class BufferList
	{
	public:
		char pad_0000[0x10];
		int32_t size;
		char pad_0014[0x4];
		void* buffer;
	};
	class Bone {
	public:
		Vector3 position;
		bool visible;
		Transform* transform;

		Bone() {
			this->position = Vector3::Zero();
			this->visible = false;
		}
		Bone(Vector3 position, bool visible) {
			this->position = position;
			this->visible = visible;
			this->transform = nullptr;
		}
		Bone(Vector3 position, bool visible, Transform* traa) {
			this->position = position;
			this->visible = visible;
			this->transform = traa;
		}
		bool visible_(Vector3 from) {
			if (this->position.empty())
				return false;

			if (!this->transform)
				return false;

			return LineOfSight(this->position, from);
		}
	};
	struct Ray {
	public:
		Vector3 m_Origin;
		Vector3 m_Direction;

		Ray() {
			this->m_Origin = Vector3(0, 0, 0);
			this->m_Direction = Vector3(0, 0, 0);
		}

		Ray(Vector3 origin, Vector3 direction) {
			this->m_Origin = origin;
			this->m_Direction = direction;
		}

		Vector3 ClosestPoint(Vector3 pos) {
			return this->m_Origin + this->m_Direction * (pos - this->m_Origin).dot_product(this->m_Direction);
		}
	};
	enum QueryTriggerInteraction {
		UseGlobal = 0,
		Ignore = 1,
		Collide = 2,
	};
	class TickInterpolator {
	private:
		struct Segment {
			Vector3 point;
			float length;
			Segment(Vector3 a, Vector3 b) {
				this->point = b;
				this->length = a.distance(b);
			}
		};
	public:
		std::vector<Segment> points = std::vector<Segment>();
		int index;
		float Length;
		Vector3 CurrentPoint;
		Vector3 StartPoint;
		Vector3 EndPoint;

		void Reset() {
			this->index = 0;
			this->CurrentPoint = this->StartPoint;
		}
		void Reset(Vector3 point) {
			this->points.clear();
			this->index = 0;
			this->Length = 0.f;
			this->EndPoint = point;
			this->StartPoint = point;
			this->CurrentPoint = point;
		}
		void AddPoint(Vector3 point) {
			Segment segment = Segment(this->EndPoint, point);
			this->points.push_back(segment);
			this->Length += segment.length;
			this->EndPoint = segment.point;
		}
		bool MoveNext(float distance) {
			float num = 0.f;
			while (num < distance && this->index < this->points.size()) {
				Segment segment = this->points[this->index];
				this->CurrentPoint = segment.point;
				num += segment.length;
				this->index++;
			}
			return num > 0.f;
		}
		bool HasNext() {
			return this->index < this->points.size();
		}
	};

	class CBounds {
	public:
		Vector3 center;
		Vector3 extents;
	};
	struct box_bounds {
		float left, right, top, bottom;

		bool empty() {
			if (this->left == 0 && this->right == 0 && this->top == 0 && this->bottom == 0)
				return true;

			if (this->left == FLT_MAX || this->right == FLT_MIN || this->top == FLT_MAX || this->bottom == FLT_MIN)
				return true;

			return false;
		}
		static box_bounds null() {
			return { 0, 0, 0, 0 };
		}
	};
	struct Color {
	public:
		float r, g, b, a;

		Color(float r, float g, float b, float a) {
			this->r = r;
			this->g = g;
			this->b = b;
			this->a = a;
		}
		static Color red() { return { 1, 0, 0, 1 }; }
		static Color green() { return { 0, 1, 0, 1 }; }
		static Color blue() { return { 0, 0, 1, 1 }; }
		static Color yellow() { return { 1, 1, 0, 1 }; }
		static Color white() { return { 1, 1, 1, 1 }; }
	};
	struct Color23 {
	public:
		float r;
		float g;
		float b;
		float a;
		Color23(float rr, float gg, float bb, float aa) {
			r = rr;
			g = gg;
			b = bb;
			a = aa;
		}
	};
	struct Line {
		Vector3 point0;
		Vector3 point1;

		Line(Vector3 point0, Vector3 point1) {
			this->point0 = point0;
			this->point1 = point1;
		}

		Line(Vector3 origin, Vector3 direction, float length) {
			this->point0 = origin;
			this->point1 = origin + direction * length;
		}
		Vector3 ClosestPoint(Vector3 pos) {
			Vector3 a = this->point1 - this->point0;
			float magnitude = a.magnitude();
			Vector3 vector = a / magnitude;
			return this->point0 + (vector * std::clamp(Vector3(pos - this->point0).dot_product(vector), 0.f, magnitude));
		}
		float Distance(Vector3 pos) {
			return (pos - this->ClosestPoint(pos)).magnitude();
		}
	};
	struct projectile_info {
		float desyncTime;
		Vector3 firstPosition;

		projectile_info(float dT, Vector3 fP) {
			this->desyncTime = dT;
			this->firstPosition = fP;
		}
	};
}


namespace il2cpp
{
	namespace methods
	{
		using il2cpp_domain_get = uintptr_t(*)();
		static il2cpp_domain_get domain_get = LI_FIND_DEF(il2cpp_domain_get);//LI_FIND_DEF(il2cpp_domain_get);

		using il2cpp_class_get_methods = uintptr_t(*)(uintptr_t, uintptr_t*);
		static auto class_get_methods = LI_FIND_DEF(il2cpp_class_get_methods);

		using il2cpp_method_get_param = uintptr_t(*)(uintptr_t, int);
		static auto method_get_param = LI_FIND_DEF(il2cpp_method_get_param);

		using il2cpp_method_get_param_count = int (*)(uintptr_t);
		static auto method_get_param_count = LI_FIND_DEF(il2cpp_method_get_param_count);

		using il2cpp_assembly_get_image = uintptr_t(*)(uintptr_t);
		static auto assembly_get_image = LI_FIND_DEF(il2cpp_assembly_get_image);

		using il2cpp_domain_get_assemblies = uintptr_t * (*)(void* domain, uintptr_t* size);
		static auto domain_get_assemblies = LI_FIND_DEF(il2cpp_domain_get_assemblies);

		using il2cpp_object_new = uintptr_t(*)(uintptr_t);
		static auto object_new = LI_FIND_DEF(il2cpp_object_new);

		using il2cpp_type_get_object = uintptr_t(*)(uintptr_t);
		static auto type_get_object = LI_FIND_DEF(il2cpp_type_get_object);

		using il2cpp_class_get_type = uintptr_t(*)(uintptr_t);
		static auto class_get_type = LI_FIND_DEF(il2cpp_class_get_type);

		using il2cpp_class_from_name = uintptr_t(*)(uintptr_t, const char*, const char*);
		static auto class_from_name = LI_FIND_DEF(il2cpp_class_from_name);

		using il2cpp_resolve_icall = uintptr_t(*)(const char*);
		static auto resolve_icall = LI_FIND_DEF(il2cpp_resolve_icall);

		using il2cpp_class_get_field_from_name = uintptr_t(*)(uintptr_t, const char*);
		static auto class_get_field_from_name = LI_FIND_DEF(il2cpp_class_get_field_from_name);

		using il2cpp_field_static_get_value = uintptr_t(*)(uintptr_t, uintptr_t*);
		static auto field_static_get_value = LI_FIND_DEF(il2cpp_field_static_get_value);

		using il2cpp_class_get_fields = uintptr_t(*)(uintptr_t, uintptr_t*);
		static auto class_get_fields = LI_FIND_DEF(il2cpp_class_get_fields);

		using il2cpp_field_get_offset = uintptr_t(*)(uintptr_t);
		static auto field_get_offset = LI_FIND_DEF(il2cpp_field_get_offset);

		using il2cpp_runtime_class_init = uintptr_t(*)(uintptr_t);
		static auto runtime_class_init = LI_FIND_DEF(il2cpp_runtime_class_init);

		using  il2cpp_class_get_method_from_name = uintptr_t(*)(uintptr_t, const char*, int);
		static il2cpp_class_get_method_from_name class_get_method_from_name = LI_FIND_DEF(il2cpp_class_get_method_from_name);
	}

	void init() {
		using il2cpp_domain_get = uintptr_t(*)();
		methods::domain_get = LI_FIND_DEF(il2cpp_domain_get);

		using il2cpp_class_get_methods = uintptr_t(*)(uintptr_t, uintptr_t*);
		methods::class_get_methods = LI_FIND_DEF(il2cpp_class_get_methods);

		using il2cpp_method_get_param = uintptr_t(*)(uintptr_t, int);
		methods::method_get_param = LI_FIND_DEF(il2cpp_method_get_param);

		using il2cpp_method_get_param_count = int (*)(uintptr_t);
		methods::method_get_param_count = LI_FIND_DEF(il2cpp_method_get_param_count);

		using il2cpp_assembly_get_image = uintptr_t(*)(uintptr_t);
		methods::assembly_get_image = LI_FIND_DEF(il2cpp_assembly_get_image);

		using il2cpp_domain_get_assemblies = uintptr_t * (*)(void* domain, uintptr_t* size);
		methods::domain_get_assemblies = LI_FIND_DEF(il2cpp_domain_get_assemblies);

		using il2cpp_class_from_name = uintptr_t(*)(uintptr_t, const char*, const char*);
		methods::class_from_name = LI_FIND_DEF(il2cpp_class_from_name);

		using il2cpp_resolve_icall = uintptr_t(*)(const char*);
		methods::resolve_icall = LI_FIND_DEF(il2cpp_resolve_icall);

		using il2cpp_class_get_field_from_name = uintptr_t(*)(uintptr_t, const char*);
		methods::class_get_field_from_name = LI_FIND_DEF(il2cpp_class_get_field_from_name);

		using il2cpp_field_static_get_value = uintptr_t(*)(uintptr_t, uintptr_t*);
		methods::field_static_get_value = LI_FIND_DEF(il2cpp_field_static_get_value);

		using il2cpp_class_get_fields = uintptr_t(*)(uintptr_t, uintptr_t*);
		methods::class_get_fields = LI_FIND_DEF(il2cpp_class_get_fields);

		using il2cpp_field_get_offset = uintptr_t(*)(uintptr_t);
		methods::field_get_offset = LI_FIND_DEF(il2cpp_field_get_offset);

		using il2cpp_object_new = uintptr_t(*)(uintptr_t);
		methods::object_new = LI_FIND_DEF(il2cpp_object_new);

		using il2cpp_type_get_object = uintptr_t(*)(uintptr_t);
		methods::type_get_object = LI_FIND_DEF(il2cpp_type_get_object);

		using il2cpp_class_get_type = uintptr_t(*)(uintptr_t);
		methods::class_get_type = LI_FIND_DEF(il2cpp_class_get_type);

		using il2cpp_runtime_class_init = uintptr_t(*)(uintptr_t);
		methods::runtime_class_init = LI_FIND_DEF(il2cpp_runtime_class_init);
	}
	uintptr_t init_class(const char* name, const char* name_space = ("")) {
		uintptr_t domain = methods::domain_get();

		if (!domain)
		{
			return NULL;
		}

		uintptr_t nrofassemblies;
		uintptr_t* assemblies;
		assemblies = methods::domain_get_assemblies((void*)domain, &nrofassemblies);

		for (int i = 0; i < nrofassemblies; i++)
		{
			uintptr_t img = methods::assembly_get_image(assemblies[i]);

			uintptr_t kl = methods::class_from_name(img, name_space, name);
			if (!kl)
				continue;

			return kl;
		}

		return NULL;
	}
	int m_strcmp(const char* s1, const char* s2) {
		while (*s1 && (*s1 == *s2))
		{
			s1++;
			s2++;
		}
		return *(const unsigned char*)s1 - *(const unsigned char*)s2;
	}
	uintptr_t method(std::string kl, std::string name, int param_count, std::string name_space = ("")) {
		uintptr_t klass = init_class(kl.c_str(), name_space.c_str());
		if (!klass)
		{
			printf("[Debug] Failed to init class in il2cpp::method: %s::%s", name_space.c_str(), kl.c_str());
			return NULL;
		}
		uintptr_t method = methods::class_get_method_from_name(klass, name.c_str(), param_count);
		return method;
	}
	uintptr_t method_alt(const char* kl, const char* name, int argument_number = -1, const char* arg_name = "", const char* name_space = (""), int selected_argument = -1) {
		uintptr_t iter = 0;
		uintptr_t f;
		auto klass = init_class(kl, name_space);

		while (f = methods::class_get_methods(klass, &iter)) {

			char* st = *reinterpret_cast<char**>(f + 0x10);

			if (m_strcmp(st, (char*)name)) {
				if (selected_argument >= 0 && arg_name) {
					uintptr_t args = *reinterpret_cast<uintptr_t*>(f + 0x28);
					int method_count = methods::method_get_param_count(f);
					if (selected_argument > method_count || (argument_number >= 0 && method_count != argument_number)) continue;

					char* argname;
					if (method_count > 0) {
						argname = *reinterpret_cast<char**>(args + (selected_argument - 1) * 0x18);
					}
					else
						argname = (char*)("-");

					if (!argname || !m_strcmp(argname, arg_name)) continue;
				}

				return f;
			}
		}
		return 0;
	}
	uintptr_t field(uintptr_t klass, const char* field_name) {
		return methods::class_get_field_from_name(klass, field_name);
	}
	uintptr_t value(const char* kl, const char* name, bool get_offset = true, const char* name_space = ("")) {
		uintptr_t klass = il2cpp::init_class(kl, name_space);

		if (!klass)
			return NULL;

		auto field = il2cpp::field(klass, name);
		if (get_offset)
		{
			uintptr_t out = 0;
			uintptr_t il2cpp_field;
			uintptr_t field_offset = NULL;

			while (il2cpp_field = methods::class_get_fields(klass, &out))
			{
				char* char_name = (char*)*reinterpret_cast<uintptr_t*>(il2cpp_field);
				if (!char_name)
					continue;

				uintptr_t offset = methods::field_get_offset(il2cpp_field);
				std::string field_name = std::string(char_name);
				if (name == field_name)
				{
					field_offset = offset;
					break;
				}
			}
			return field_offset;
		}

		uintptr_t static_value;
		methods::field_static_get_value(field, &static_value);
		if (static_value)
			return static_value;

		return NULL;
	}
	uintptr_t type_object(const char* name_space, const char* name) {
		auto klass = il2cpp::init_class(name, name_space);
		return il2cpp::methods::type_get_object(il2cpp::methods::class_get_type(klass));
	}
}
