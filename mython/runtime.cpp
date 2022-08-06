#include "runtime.h"

#include <cassert>
#include <optional>
#include <sstream>

using namespace std;

namespace runtime {

    ObjectHolder::ObjectHolder(std::shared_ptr<Object> data)
        : data_(std::move(data)) {
    }

    void ObjectHolder::AssertIsValid() const {
        assert(data_ != nullptr);
    }

    ObjectHolder::ObjectHolder(const ObjectHolder& other) 
        : data_(other.data_)
    {
    }
    ObjectHolder& ObjectHolder::operator = (const ObjectHolder& other) {
        data_ = other.data_;
        return *this;
    }

    ObjectHolder::ObjectHolder(ObjectHolder&& other) noexcept
        : data_(move(other.data_))
    {

    }

    ObjectHolder& ObjectHolder::operator = (ObjectHolder&& other) noexcept {
        data_ = move(other.data_);
        return *this;
    }

    ObjectHolder ObjectHolder::Share(Object& object) {
        // Возвращаем невладеющий shared_ptr (его deleter ничего не делает)
        return ObjectHolder(std::shared_ptr<Object>(&object, [](auto* /*p*/) { /* do nothing */ }));
    }

    ObjectHolder ObjectHolder::None() {
        return ObjectHolder();
    }

    Object& ObjectHolder::operator*() const {
        AssertIsValid();
        return *Get();
    }

    Object* ObjectHolder::operator->() const {
        AssertIsValid();
        return Get();
    }

    Object* ObjectHolder::Get() const {
        return data_.get();
    }

    ObjectHolder::operator bool() const {
        return Get() != nullptr;
    }

    bool IsTrue(const ObjectHolder& object) {
        if (!object) {
            return false;
        }
        if (auto* ptr = object.TryAs<Bool>()) {
            return ptr->GetValue();
        }
        if (auto* ptr = object.TryAs<Number>()) {
            return ptr->GetValue();
        }
        if (auto* ptr = object.TryAs<String>()) {
            return !((ptr->GetValue()).empty());
        }
        return false;
    }

    void ClassInstance::Print(std::ostream& os, Context& context) {
        if (class_.GetMethod("__str__")) {
            os << Call("__str__", {}, context).TryAs<String>()->GetValue();
            return;
        }
        os << this;
    }

    bool ClassInstance::HasMethod(const std::string& method, size_t argument_count) const {
        if (const Method* method_ptr = class_.GetMethod(method)) {
            if (method_ptr->formal_params.size() == argument_count) {
                return true;
            }
        }
        return false;
    }

    Closure& ClassInstance::Fields() {
        return fields_;
    }

    const Closure& ClassInstance::Fields() const {
        return fields_;
    }

    ClassInstance::ClassInstance(const Class& cls)
        : class_(cls)
    {

    }

    ObjectHolder ClassInstance::Call(const std::string& method,
        const std::vector<ObjectHolder>& actual_args,
        Context& context) {

        if (!HasMethod(method, actual_args.size())) {
            throw std::runtime_error("method not found"s);
        }

        const Method* method_ptr = class_.GetMethod(method);

        Closure closure;
        closure["self"] = ObjectHolder::Share(*this);
        for (size_t i = 0; i < actual_args.size(); ++i) {
            closure[method_ptr->formal_params[i]] = actual_args[i];
        }
        
        ObjectHolder result = method_ptr->body->Execute(closure, context);
        if (closure.at("self").Get() != this) {
            return closure.at("self");
        } 
        return result;   
    }

    Class::Class(std::string name, std::vector<Method>&& methods, const Class* parent)
        : parent_(parent), name_(name), methods_(move(methods)) 
    {
        for (size_t i = 0; i < methods_.size(); ++i) {
            name_to_method_index_[methods_[i].name] = i;
        }
    }

    const Method* Class::GetMethod(const std::string& name) const {
        if (name_to_method_index_.count(name)) {
            return &methods_[name_to_method_index_.at(name)];
        }
        if (parent_) {
            return parent_->GetMethod(name);
        }
        return nullptr;
    }

    [[nodiscard]] const std::string& Class::GetName() const {
        return name_;
    }

    void Class::Print(ostream& os, [[maybe_unused]] Context& context) {
        os << "Class "sv << GetName();
    }

    void Bool::Print(std::ostream& os, [[maybe_unused]] Context& context) {
        os << (GetValue() ? "True"sv : "False"sv);
    }

    bool Equal(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {

        if (!lhs && !rhs) {
            return true;
        }

        if (auto* left = lhs.TryAs<String>()) {
            if (auto* right = rhs.TryAs<String>()) {
                return left->GetValue() == right->GetValue();
            }
            throw std::runtime_error("Cannot compare objects for equality"s);
        }

        if (auto* left = lhs.TryAs<Number>()) {
            if (auto* right = rhs.TryAs<Number>()) {
                return left->GetValue() == right->GetValue();
            }
            throw std::runtime_error("Cannot compare objects for equality"s);
        }

        if (auto* left = lhs.TryAs<Bool>()) {
            if (auto* right = rhs.TryAs<Bool>()) {
                return left->GetValue() == right->GetValue();
            }
            throw std::runtime_error("Cannot compare objects for equality"s);
        }

        if (auto* left = lhs.TryAs<ClassInstance>()) {
            if (left->HasMethod("__eq__", 1)) {
                return IsTrue(left->Call("__eq__", { rhs }, context));
            }
        }

        throw std::runtime_error("Cannot compare objects for equality"s);
    }

    bool Less(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {

        if (auto* left = lhs.TryAs<String>()) {
            if (auto* right = rhs.TryAs<String>()) {
                return left->GetValue() < right->GetValue();
            }
            throw std::runtime_error("Cannot compare objects for less"s);
        }

        if (auto* left = lhs.TryAs<Number>()) {
            if (auto* right = rhs.TryAs<Number>()) {
                return left->GetValue() < right->GetValue();
            }
            throw std::runtime_error("Cannot compare objects for less"s);
        }

        if (auto* left = lhs.TryAs<Bool>()) {
            if (auto* right = rhs.TryAs<Bool>()) {
                return left->GetValue() < right->GetValue();
            }
            throw std::runtime_error("Cannot compare objects for less"s);
        }

        if (auto* left = lhs.TryAs<ClassInstance>()) {
            if (left->HasMethod("__lt__", 1)) {
                return IsTrue(left->Call("__lt__", { rhs }, context));
            }
        }

        throw std::runtime_error("Cannot compare objects for less"s);
    }

    bool NotEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        return !Equal(lhs, rhs, context);
    }

    bool Greater(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        return !(Less(lhs, rhs, context) || Equal(lhs, rhs, context));
    }

    bool LessOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        return !Greater(lhs, rhs, context);
    }

    bool GreaterOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
        return !Less(lhs, rhs, context);
    }

}  // namespace runtime
