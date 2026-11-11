import { clu } from '../../common/clu'
// CSP violation reporter (platform F5): every violation on a Clu surface goes to the log at error,
// so it shows in the logs and fails e2e. The UI root has its own listener; the overlay and internal pages use this.
export function reportCspViolations(doc: Document = document): () => void {
  const onViolation = (e: SecurityPolicyViolationEvent) =>
    void clu().log.write({ level: 'error', scope: 'csp', msg: 'securitypolicyviolation', data: { directive: String(e.violatedDirective).slice(0, 64) } })
  doc.addEventListener('securitypolicyviolation', onViolation)
  return () => doc.removeEventListener('securitypolicyviolation', onViolation)
}
