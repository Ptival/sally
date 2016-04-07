(* Abstract syntax for the lispy style of the .mcmt files read by Sally *)

type state_identifier = string
type system_identifier = string
type state_type_identifier = string
type transition_identifier = string

type sally_type = 
	| Real
	| Bool

type sally_condition =
	| Equality of string * string
	| Or of sally_condition * sally_condition
	| And of sally_condition * sally_condition
	| Not of sally_condition
	| Assignation of string * string

type variable_declaration = string * sally_type

type state_type = state_type_identifier * (variable_declaration list)

type state = state_identifier * state_type_identifier * sally_condition

type transition = transition_identifier * state_type_identifier * sally_condition

type transition_system = system_identifier * state_type * (* initial state *) state * transition

type query = transition_system * sally_condition

let print_query ch q =
	let ft = Format.formatter_of_out_channel ch in
	Format.fprintf ft "hello@."
