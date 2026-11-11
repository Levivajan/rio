// Command bar input cap (04-command F12), shared by the classifier and the IPC contract without pulling in the TLD set.
export const MAX_INPUT = 8192
/** Longest input sent to the engine for suggestions; longer input is never sent (04-command F7). */
export const MAX_SUGGEST_QUERY = 256
