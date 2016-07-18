open Ast.Sal_ast;;
open Format;;

(* Inline functions, constant declarations, let-statements *)

module StrMap = Map.Make (struct type t = string let compare = compare end)

type to_inline = Type of sal_type | Val of sal_expr | Fun of string list * sal_expr

let get_conds ifs = List.map fst ifs

(* turn an i b_1 then b_2 else if ... then b_n-1 else b_n expression into a list of
booleans *)
let flatten_cond ifs els =
  let rec expand_conds conds res =
    match conds with
    | [c] -> Not c::res
    | c::cs -> expand_conds (List.map (fun x -> And (Not c, x)) cs) (c::res) in
  List.rev_map2 (fun x y -> And(x, y)) (expand_conds (List.map fst ifs) []) (els::(List.rev_map snd ifs))

let rec preproc_expr expr ctx =
  match expr with
  | Ident str -> printf "ident_p: %s\n" str;
      if StrMap.mem str ctx
      then
        match StrMap.find str ctx with
          Val expr -> preproc_expr expr ctx
      else Ident str
  | Funcall (str, exprs) -> printf "funcall: %s\n" str;
      if StrMap.mem str ctx
      then match StrMap.find str ctx with
        Fun (strs, expr) ->
          List.iter (printf "repl: %s\n") strs;
          let rec add_kvs ctx ks vs =
            (match (ks,vs) with
            | ([],[]) -> ctx
            | (k::ks, v::vs) -> add_kvs (StrMap.add k (Val v) ctx) ks vs) in
          preproc_expr expr (add_kvs ctx strs exprs)
      else Funcall (str, exprs)
  | Add (e1, e2) -> Add (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Sub (e1, e2) -> Sub (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Mul (e1, e2) -> Mul (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Div (e1, e2) -> Div (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Ge (e1, e2) -> Ge (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Gt (e1, e2) -> Gt (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Le (e1, e2) -> Le (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Lt (e1, e2) -> Lt (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Eq (e1, e2) -> Eq (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Neq (e1, e2) -> Neq (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Not e -> Not (preproc_expr e ctx)
  | And (e1, e2) -> And (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Or (e1, e2) -> Or (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Xor (e1, e2) -> Xor (preproc_expr e1 ctx, preproc_expr e2 ctx)
  | Implies (e1, e2) ->
      Or (Not (preproc_expr e1 ctx), (preproc_expr e2 ctx))
  | Iff (e1, e2) -> Iff (preproc_expr e1 ctx, preproc_expr e2 ctx)
  (* need to handle lets *)
  | other -> other;;

let preproc_assigns assigns ctx =
  let preproc_assign assign =
    match assign with
    | Assign (e1, e2) -> Assign (e1, preproc_expr e2 ctx)
    | other -> other in
  List.map preproc_assign assigns;;

let rec preproc_guarded gc ctx =
  match gc with
  | ExistentialGuarded (decl, gc) -> ExistentialGuarded (decl,(preproc_guarded gc ctx))
  | Guarded (cond, assigns) -> Guarded (preproc_expr cond ctx, preproc_assigns assigns ctx)
  | Default assigns -> Default (preproc_assigns assigns ctx);;

let preproc_transition st ctx =
  match st with
  | NoTransition -> NoTransition
  | Assignments als -> Assignments als
  | GuardedCommands gls -> GuardedCommands (List.map (fun x -> preproc_guarded x ctx) gls);;

let rec preproc_defs ds ctx res =
  match ds with
  | [] -> res
  | (Type_def (str, st))::ds -> preproc_defs ds (StrMap.add str (Type st) ctx) res
  | (Constant_def (str, st, expr))::ds -> preproc_defs ds (StrMap.add str (Val (preproc_expr expr ctx)) ctx) res
  | (Function_def (str, sdl, st, expr))::ds ->
      preproc_defs ds (StrMap.add str (Fun (List.flatten (List.map fst sdl), preproc_expr expr ctx)) ctx) res
  | (Module_def (str, sal_mod))::ds ->
      preproc_defs ds ctx
        ((Module_def (str, { sal_mod with
          initialization = preproc_assigns (sal_mod.initialization) ctx;
          definition = preproc_assigns (sal_mod.definition) ctx;
          invariant =
            ( match sal_mod.invariant with
              | Some e -> Some (preproc_expr e ctx)
              | None -> None );
          transition = preproc_transition (sal_mod.transition) ctx }))::res)
  | _::ds -> preproc_defs ds ctx res;;
  
let preproc sal_ctx =
  let defs = preproc_defs sal_ctx.definitions StrMap.empty [] in
  { sal_ctx with definitions = defs };;
