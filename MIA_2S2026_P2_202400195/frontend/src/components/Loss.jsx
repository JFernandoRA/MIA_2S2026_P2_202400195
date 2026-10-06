import { useState, useEffect } from 'react'
import { api } from '../api'
import PartitionPicker from './PartitionPicker'

const MAX_CELLS = 600

function BitmapGrid({ title, bits }) {
  const used = bits.split('').filter((b) => b === '1').length
  return (
    <div className="bitmap">
      <div className="pane-head">
        <span>{title}</span>
        <span>{used} usados de {bits.length}</span>
      </div>
      <div className="bitmap-grid">
        {bits.slice(0, MAX_CELLS).split('').map((b, i) => <span key={i} className={b === '1' ? 'bit on' : 'bit'} title={`#${i}: ${b}`} />)}
      </div>
      {bits.length > MAX_CELLS && <p className="note">Se muestran los primeros {MAX_CELLS}.</p>}
    </div>
  )
}

function Snapshot({ label, bm }) {
  return (
    <div className="snapshot">
      <h2>{label}</h2>
      <BitmapGrid title="bitmap de inodos" bits={bm.inodes} />
      <BitmapGrid title="bitmap de bloques" bits={bm.blocks} />
    </div>
  )
}

export default function Loss({ refreshKey, onExecuted }) {
  const [id, setId] = useState('')
  const [current, setCurrent] = useState(null)
  const [before, setBefore] = useState(null)
  const [after, setAfter] = useState(null)
  const [message, setMessage] = useState('')
  const [error, setError] = useState('')

  useEffect(() => {
    setBefore(null)
    setAfter(null)
    setMessage('')
  }, [id])

  useEffect(() => {
    if (!id) return setCurrent(null)
    api.bitmaps(id).then((r) => {
      setError(r.ok ? '' : r.error)
      setCurrent(r.ok ? r.bitmaps : null)
    })
  }, [id, refreshKey])

  async function runLoss() {
    if (!window.confirm(`Se limpiarán con \\0 los bitmaps, inodos y bloques de ${id}. Los datos se perderán. ¿Continuar?`)) return
    const pre = await api.bitmaps(id)
    const r = await api.execute(`loss -id=${id}`)
    const post = await api.bitmaps(id)
    setBefore(pre.bitmaps)
    setAfter(post.bitmaps)
    setMessage(r.output.trim())
    onExecuted()
  }

  return (
    <div className="viewer">
      <div className="viewer-head">
        <h1 className="card-title">Simulación de pérdida (LOSS)</h1>
        <div className="viewer-actions">
          <PartitionPicker value={id} onChange={setId} onlyExt3 refreshKey={refreshKey} />
          {id && <button className="btn btn-danger" onClick={runLoss}>ejecutar loss</button>}
        </div>
      </div>
      {error && <p className="form-error">{error}</p>}
      {message && <p className="viewer-hint">{message}</p>}
      {before && after ? (
        <div className="snapshots">
          <Snapshot label="antes" bm={before} />
          <Snapshot label="después" bm={after} />
        </div>
      ) : (
        current && <div className="snapshots"><Snapshot label="estado actual" bm={current} /></div>
      )}
    </div>
  )
}
