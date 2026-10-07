import { useState, useEffect } from 'react'
import { api, formatBytes } from '../api'

const IMAGE_EXT = ['png', 'jpg', 'jpeg', 'svg', 'gif']

const pad = (n) => String(n).padStart(2, '0')
function formatDate(ts) {
  const d = new Date(ts * 1000)
  return `${pad(d.getDate())}/${pad(d.getMonth() + 1)}/${d.getFullYear()} ${pad(d.getHours())}:${pad(d.getMinutes())}`
}

function Preview({ report }) {
  const [text, setText] = useState('')
  const url = api.reportUrl(report.path, report.timestamp)
  const isImage = IMAGE_EXT.includes(report.ext)
  const isPdf = report.ext === 'pdf'

  useEffect(() => {
    setText('')
    if (isImage || isPdf) return
    fetch(url).then((r) => r.text()).then(setText).catch(() => setText('# no se pudo cargar el reporte'))
  }, [url, isImage, isPdf])

  return (
    <div className="report-preview">
      <div className="pane-head">
        <span>{report.path}</span>
        <a className="btn" href={url} target="_blank" rel="noreferrer">abrir en pestaña nueva</a>
      </div>
      {isImage && <div className="report-image"><img src={url} alt={`reporte ${report.name}`} /></div>}
      {isPdf && <iframe className="report-pdf" src={url} title={report.file} />}
      {!isImage && !isPdf && <pre className="editor output report-text">{text || 'cargando…'}</pre>}
    </div>
  )
}

export default function Reports({ refreshKey }) {
  const [reports, setReports] = useState([])
  const [selected, setSelected] = useState(null)
  const [error, setError] = useState('')

  useEffect(() => {
    api.reports().then((r) => {
      setError('')
      setReports(r.reports)
      setSelected((cur) => r.reports.find((x) => x.path === cur?.path) || r.reports[0] || null)
    }).catch(() => setError('No se pudo conectar con el backend'))
  }, [refreshKey])

  return (
    <div className="viewer">
      <div className="viewer-head">
        <h1 className="card-title">Reportes</h1>
        <p className="viewer-hint">Generados con el comando rep (Graphviz). El más reciente aparece primero.</p>
      </div>
      {error && <p className="form-error">{error}</p>}
      {!error && reports.length === 0 && (
        <p className="note">Aún no hay reportes. Ejemplo en la consola: <code>rep -id=951A -path=/home/fercho/reportes/mbr.png -name=mbr</code></p>
      )}
      {reports.length > 0 && (
        <div className="reports-layout">
          <ul className="report-list">
            {reports.map((r) => (
              <li key={r.path}>
                <button className={`report-item ${selected?.path === r.path ? 'is-selected' : ''}`} onClick={() => setSelected(r)}>
                  <span className="chip">{r.name}</span>
                  <span className="report-file">{r.file}</span>
                  <span className="dim report-meta">{r.id} · {formatBytes(r.size)} · {formatDate(r.timestamp)}</span>
                </button>
              </li>
            ))}
          </ul>
          {selected && <Preview report={selected} />}
        </div>
      )}
    </div>
  )
}
