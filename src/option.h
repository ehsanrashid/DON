/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef OPTION_H_INCLUDED
#define OPTION_H_INCLUDED

#include <functional>
#include <iosfwd>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "misc.h"

namespace DON {

class Options;

class Option {
   public:
    using ChangeInfo = std::optional<std::string>;
    using OnChange   = std::function<ChangeInfo(const Option&)>;

    virtual ~Option() = default;

    std::string_view current_value() const noexcept;

    std::string_view default_value() const noexcept;

    virtual std::string_view type() const noexcept = 0;

    virtual void print(std::ostream& os) const noexcept = 0;

    virtual operator int() const noexcept;

    virtual operator std::string_view() const noexcept;

    virtual void operator=(std::string_view value) noexcept = 0;

   protected:
    explicit Option(std::string_view str, OnChange&& onCng = nullptr) noexcept;

    void on_change() noexcept;

   private:
    const std::string defaultValue;

   protected:
    std::string currentValue;

   private:
    OnChange onChange;

    const Options* optionsPtr = nullptr;

    friend class Options;
};

using OnChange = Option::OnChange;

using OptionPtr = std::unique_ptr<Option>;

std::ostream& operator<<(std::ostream& os, const Option& option) noexcept;

class ButtonOption final: public Option {
   public:
    explicit ButtonOption(OnChange&& onCng = nullptr) noexcept;

    std::string_view type() const noexcept override;

    void print(std::ostream& os) const noexcept override;

    void operator=(std::string_view value) noexcept override;
};

class CheckOption final: public Option {
   public:
    explicit CheckOption(bool value, OnChange&& onCng = nullptr) noexcept;

    std::string_view type() const noexcept override;

    void print(std::ostream& os) const noexcept override;

    operator int() const noexcept override;

    void operator=(std::string_view value) noexcept override;

   private:
    static std::string normalize(std::string str) noexcept;
};

class StringOption final: public Option {
   public:
    explicit StringOption(std::string_view str, OnChange&& onCng = nullptr) noexcept;

    std::string_view type() const noexcept override;

    void print(std::ostream& os) const noexcept override;

    operator std::string_view() const noexcept override;

    void operator=(std::string_view value) noexcept override;

   private:
    static std::string normalize(std::string str) noexcept;
};

class SpinOption final: public Option {
   public:
    SpinOption(int v, int minV, int maxV, OnChange&& onCng = nullptr) noexcept;

    std::string_view type() const noexcept override;

    void print(std::ostream& os) const noexcept override;

    operator int() const noexcept override;

    void operator=(std::string_view value) noexcept override;

   private:
    const int minValue;
    const int maxValue;
};

class ComboOption final: public Option {
   public:
    ComboOption(std::string_view str, StringViews&& vrs, OnChange&& onCng = nullptr) noexcept;

    std::string_view type() const noexcept override;

    void print(std::ostream& os) const noexcept override;

    operator std::string_view() const noexcept override;

    void operator=(std::string_view value) noexcept override;

   private:
    bool contains(std::string_view value) const noexcept;

    const Strings vars;
};

namespace OptionFactory {

OptionPtr button(OnChange&& onCng = nullptr) noexcept;

OptionPtr check(bool b, OnChange&& onCng = nullptr) noexcept;

OptionPtr string(std::string_view str, OnChange&& onCng = nullptr) noexcept;

OptionPtr spin(int v, int minV, int maxV, OnChange&& onCng = nullptr) noexcept;

OptionPtr combo(std::string_view str, StringViews vars, OnChange&& onCng = nullptr) noexcept;

}  // namespace OptionFactory

class Options final {
   public:
    // clang-format off
    // Stores the option name view and the polymorphic option, preserving the original name case.
    using Entry    = std::pair<std::string_view, OptionPtr>;
    // Preserves insertion order and the original name case.
    using List     = std::list<Entry>;
    // Provides fast case-insensitive name lookup, count and removal.
    using IndexMap = std::unordered_map<std::string_view, List::iterator, CaseInsensitiveHash, CaseInsensitiveEqual>;
    // Provides fast case-insensitive uniqueness and membership checks.
    using Set      = std::unordered_set<std::string_view, CaseInsensitiveHash, CaseInsensitiveEqual>;
    // clang-format on

    using Info   = std::optional<std::string_view>;
    using OnInfo = std::function<void(Info)>;

    Options() noexcept = default;

    auto begin() noexcept;
    auto end() noexcept;
    auto begin() const noexcept;
    auto end() const noexcept;

    usize size() const noexcept;
    bool  empty() const noexcept;

    bool contains(Set::const_iterator setItr) const noexcept;
    bool contains(std::string_view name) const noexcept;

    usize count(std::string_view name) const noexcept;

    auto find(std::string_view name) noexcept;
    auto find(std::string_view name) const noexcept;

    // Adds an option with the specified name to the Options.
    //
    // Options are stored in insertion order and indexed by name.
    // Returns false if an option with the specified name already exists.
    bool add(std::string_view name, OptionPtr option) noexcept;

    // Removes the option with the specified name from the Options.
    //
    // Returns false if no option with the specified name exists.
    bool remove(std::string_view name) noexcept;

    void setoption(std::string_view name, std::string_view value) noexcept;

    const Option& operator[](std::string_view name) const noexcept;

    void set_on_info(OnInfo&& onInf) noexcept;

    void on_info(Info info) const noexcept;

   private:
    Options(const Options&) noexcept            = delete;
    Options& operator=(const Options&) noexcept = delete;
    Options(Options&&) noexcept                 = delete;
    Options& operator=(Options&&) noexcept      = delete;

    List     list;
    IndexMap indexMap;
    Set      set;

    OnInfo onInfo;
};

std::ostream& operator<<(std::ostream& os, const Options& options) noexcept;

}  // namespace DON

#endif  // OPTION_H_INCLUDED
