//===-- stats.inc -----------------------------------------------*- C++ -*-===//
//
// <insert license>
//
//===----------------------------------------------------------------------===//
//
// This file contains the statistics that are used in cliver. A statistic is
// defined by a variable name, full name and short name. The full name and
// short name are stored as strings. See cliver/Statistics.h for example usage.
//
//===----------------------------------------------------------------------===//

XSTAT( round_number,                             RoundNumber,                    Rn       ) 
XSTAT( round_time,                               RoundUserTime,                  RTm      ) 
XSTAT( round_real_time,                          RoundRealTime,                  RRTm     ) 
XSTAT( round_real_time_pass_one,                 RoundRealTimePassOne,           RRTmP1   )
XSTAT( round_sys_time,                           RoundSysTime,                   RSTm     ) 
XSTAT( merged_states,                            MergedStates,                   MSts     ) 
XSTAT( merge_time,                               MergerTime,                     MTm      ) 
XSTAT( searcher_time,                            SearcherTime,                   STm      ) 
XSTAT( fork_time,                                ForkTime,                       FTm      ) 
XSTAT( round_instructions,                       InstructionCount,               ICnt     ) 
XSTAT( round_instructions_pass_one,              InstructionCountPassOne,        ICntP1   )
XSTAT( recv_round_instructions,                  RecvInstructionCount,           RCnt     ) 
XSTAT( rebuild_time,                             RebuildTime,                    RBTm     ) 
XSTAT( bindings_solve_time,                      BindingsSolveTime,              BSTm     )
XSTAT( execution_tree_time,                      ExecTreeTime,                   ETTm     ) 
XSTAT( edit_distance_time,                       EditDistTime,                   EDTm     ) 
XSTAT( edit_distance_build_time,                 EditDistBuildTime,              EDBdTm   ) 
XSTAT( edit_distance_hint_time,                  EditDistHintTime,               EDHtTm   ) 
XSTAT( edit_distance_stat_time,                  EditDistStatTime,               EDStTm   ) 
XSTAT( stage_count,                              StageCount,                     StgCnt   ) 
XSTAT( state_clone_count,                        StateCloneCount,                StClnCnt ) 
XSTAT( state_remove_count,                       StateRemoveCount,               StRemCnt ) 
XSTAT( socket_event_type,                        SocketEventType,                SETy     )
XSTAT( socket_event_size,                        SocketEventSize,                SESz     ) 
XSTAT( socket_event_timestamp,                   SocketEventTimestamp,           SETm     )
XSTAT( valid_path_instructions,                  ValidPathInstructionCount,      VPICnt   ) 
XSTAT( symbolic_variable_count,                  SymbolicVariableCount,          SVCnt    ) 
XSTAT( pass_count,                               PassCount,                      PassCnt  ) 
XSTAT( edit_distance,                            EditDist,                       EDist    ) 
XSTAT( edit_distance_k,                          EditDistK,                      EDK      ) 
XSTAT( edit_distance_medoid_count,               EditDistMedoidCount,            EDMedCnt ) 
XSTAT( edit_distance_self_first_medoid,          EditDistSelfFirstMedoid,        EDSFMed  ) 
XSTAT( edit_distance_self_last_medoid,           EditDistSelfLastMedoid,         EDSLMed  ) 
XSTAT( edit_distance_self_socket_event,          EditDistSelfSocketEvent,        EDSLSE   ) 
XSTAT( edit_distance_socket_event_first_medoid,  EditDistSocketEventFirstMedoid, EDSEFMed ) 
XSTAT( edit_distance_socket_event_last_medoid,   EditDistSocketEventLastMedoid,  EDSELMed ) 

