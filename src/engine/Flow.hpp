#pragma once

#include <vector>

namespace sokoban::flow {

// Overload set for std::visit over event/command variants:
//   std::visit(flow::Overloaded { [](const A&) {...}, [](const B&) {...} }, v);
template <typename... Fns>
struct Overloaded : Fns... {
    using Fns::operator()...;
};
template <typename... Fns>
Overloaded(Fns...) -> Overloaded<Fns...>;

// Minimal pure state machine shared by UI flows (and available to future
// gameplay flows). A machine owns a State value; Derived implements
//
//   void reduce(State& state, const Event& event, const Facts& facts,
//               std::vector<Command>& commands);
//
// reduce() must be pure apart from mutating `state`: it inspects the event
// and the caller-supplied facts snapshot, updates its own state, and appends
// commands. It never performs effects - the caller executes the returned
// commands - which is what makes every transition unit-testable without any
// engine systems present.
//
// Conventions for new flows:
// - Event and Command are std::variants of small structs named for intent
//   ("SwitchSlot", not "OnSlotButton").
// - Facts is a plain snapshot struct; the flow never reaches back into live
//   systems.
// - Emitting nothing is a valid, meaningful outcome (e.g. an overlay that
//   requires an explicit choice ignores Back).
template <typename Derived, typename State, typename Event, typename Command,
    typename Facts>
class Machine {
public:
    using StateType = State;
    using EventType = Event;
    using CommandType = Command;
    using FactsType = Facts;

    [[nodiscard]] const State& state() const { return state_; }

    [[nodiscard]] std::vector<Command> handle(const Event& event, const Facts& facts)
    {
        std::vector<Command> commands;
        static_cast<Derived&>(*this).reduce(state_, event, facts, commands);
        return commands;
    }

protected:
    State state_ {};
};

} // namespace sokoban::flow
