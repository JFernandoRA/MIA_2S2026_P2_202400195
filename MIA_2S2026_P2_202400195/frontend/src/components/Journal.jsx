import { useState, useEffect } from 'react'
import { api } from '../api'
import PartitionPicker from './PartitionPicker'

const pad = (n) => String(n).padStart(2, '0')

function formatDate(ts) {
  const d = new Date(ts * 1000)
  return `${pad(d.getDate())}/${pad(d.getMonth() + 1)}/${d.getFullYear()} ${pad(d.getHours())}:${pad(d.getMinutes())}`
}

export default function Journal({ refreshKey, defaultId }) {
  const [id, setId] = useState(defaultId || '')
  const [entries, setEntries] = useState([])
  const [filter, setFilter] = useState('')
  const [error, setError] = useState('')

  useEffect(() => {
    if (!id) return setEntries([])
    api.journaling(id).then((r) => {
      setError(r.ok ? '' : r.error)
      setEntries(r.entries || [])
    }).catch(() => setError('No se pudo conectar con el backend'))
  }, [id, refreshKey])

  const shown = entries.filter((e) => !filter || e.operation === filter)
  const ops = [...new Set(entries.map((e) => e.operation))]

  return (
    <div className="viewer">
      <div className="viewer-head">
        <h1 className="card-title">Journaling</h1>
        <div className="viewer-actions">
          <PartitionPicker value={id} onChange={setId} onlyExt3 refreshKey={refreshKey} />
          {ops.length > 1 && (
            <select className="select" value={filter} onChange={(e) => setFilter(e.target.value)}>
              <option value="">todas las operaciones</option>
              {ops.map((o) => <option key={o} value={o}>{o}</option>)}
            </select>
          )}
        </div>
      </div>
      {error && <p className="form-error">{error}</p>}
      {id && !error && <p className="viewer-hint">{entries.length} transacciones registradas en {id}</p>}
      {shown.length > 0 && (
        <div className="table-wrap">
          <table className="table">
            <thead>
              <tr><th>#</th><th>Operación</th><th>Path</th><th>Contenido</th><th>Fecha</th></tr>
            </thead>
            <tbody>
              {shown.map((e) => (
                <tr key={e.count}>
                  <td className="dim">{e.count}</td>
                  <td><span className={`chip chip-${e.operation}`}>{e.operation}</span></td>
                  <td>{e.path}</td>
                  <td className="cell-content">{e.content || '—'}</td>
                  <td className="nowrap">{formatDate(e.timestamp)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  )
}
