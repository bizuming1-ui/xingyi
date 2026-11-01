import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import "./index.css";
import App from "./App";
import { sourceFiles } from "./lib/sources";

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App files={sourceFiles} />
  </StrictMode>
);
